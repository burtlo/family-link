"""Single-process disk archive: immutable manifests/media, resumable PCM chunks.

Media is never the in-memory source of truth. A same-filesystem directory rename
publishes an entire recipient set. SQLite holds only mutable per-user state.
"""
from __future__ import annotations

import errno
import hashlib
import json
import os
import re
import shutil
import sqlite3
import threading
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import BinaryIO

from demos.server.h34_message_store.pcm_wav import finalize_pcm_chunks_to_wav

BLOCK = 64 * 1024
MAX_AUDIO = 6 * 1024 * 1024
MAX_CHUNK = 192 * 1024
MAX_DURATION = 180_000
MAX_SKETCH = 64 * 1024  # existing bounded FLSK1, not FLSK2
MAX_CHUNKS = 90 * 5  # bounded metadata; 2 s chunks need only 90


class ArchiveError(Exception):
    def __init__(self, status: int, message: str):
        self.status = status
        super().__init__(message)


def identifier(value: str) -> str:
    try:
        if str(uuid.UUID(value)) != value:
            raise ValueError()
    except (ValueError, TypeError, AttributeError):
        raise ArchiveError(400, "invalid message identity") from None
    return value


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as source:
        while block := source.read(BLOCK):
            digest.update(block)
    return digest.hexdigest()


def sync_dir(path: Path) -> bool:
    # Windows cannot open directories using os.open. File fsync still applies.
    if os.name == 'nt':
        return False
    try:
        fd = os.open(path, os.O_RDONLY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)
    except OSError as exc:
        if exc.errno not in (errno.EINVAL, errno.ENOTSUP, errno.EBADF, errno.EISDIR):
            raise
        return False
    return True


def atomic_json(path: Path, value: dict) -> None:
    tmp = path.with_name('.' + uuid.uuid4().hex + '.tmp')
    try:
        with tmp.open('x', encoding='utf-8') as out:
            json.dump(value, out, sort_keys=True, separators=(',', ':'))
            out.write('\n')
            out.flush()
            os.fsync(out.fileno())
        os.replace(tmp, path)
        sync_dir(path.parent)
    finally:
        tmp.unlink(missing_ok=True)


def crash_at(boundary: str) -> None:
    # Qualification subprocesses only. Never set this on the household server.
    if os.environ.get('FAMILY_LINK_PRODUCT_CRASH_AT') == boundary:
        os._exit(86)


class MessageArchive:
    def __init__(self, root: Path, *, free_floor: int = 64 * 1024 * 1024):
        self.root = root.expanduser().resolve()
        if free_floor < 0:
            raise ValueError("free disk floor must be nonnegative")
        self.free_floor = free_floor
        self.lock = threading.RLock()
        self.index: dict[str, dict] = {}
        self.paths: dict[str, Path] = {}
        self.next_seq: dict[str, int] = {}
        self.root.mkdir(parents=True, exist_ok=True, mode=0o700)
        self._owner = (self.root / '.server.lock').open('a+b')
        try:
            if os.name == 'nt':
                import msvcrt
                self._owner.seek(0)
                self._owner.write(b'0')
                self._owner.flush()
                self._owner.seek(0)
                msvcrt.locking(self._owner.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self._owner.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            for name in ('incoming', 'messages', 'state'):
                (self.root / name).mkdir(exist_ok=True)
            self.directory_fsync_supported = sync_dir(self.root)
            self.db = sqlite3.connect(self.root / 'state' / 'user-state.sqlite3', check_same_thread=False)
            self.db.execute('PRAGMA synchronous=FULL')
            self.db.execute('CREATE TABLE IF NOT EXISTS user_state (user_id TEXT PRIMARY KEY, payload TEXT NOT NULL)')
            self.db.commit()
            self._recover()
        except BaseException:
            if hasattr(self, 'db'):
                self.db.close()
            self._owner.close()
            raise

    def close(self) -> None:
        self.db.close()
        self._owner.close()

    def _recover(self) -> None:
        for path in sorted((self.root / 'messages').glob('*/*/*')):
            if not path.is_dir():
                continue
            try:
                if not path.resolve().is_relative_to(self.root) or path.is_symlink():
                    raise ValueError('message directory escapes archive')
                manifest = json.loads((path / 'manifest.json').read_text())
                if not (path / 'complete').is_file() or manifest['state'] != 'complete':
                    raise ValueError('missing completion marker')
                if manifest['schema'] != 'family-product-manifest/1' or identifier(manifest['message_id']) != path.name:
                    raise ValueError('manifest identity/schema')
                if not isinstance(manifest.get('audio'), dict) or not isinstance(manifest.get('duration_ms'), int) or not 0 < manifest['duration_ms'] <= MAX_DURATION:
                    raise ValueError('invalid audio metadata')
                if not isinstance(manifest.get('from_user'), str) or not isinstance(manifest.get('from_label'), str):
                    raise ValueError('invalid sender metadata')
                if not manifest['recipients'] or len({r['user_id'] for r in manifest['recipients']}) != len(manifest['recipients']):
                    raise ValueError('invalid recipients')
                for asset, filename in (('audio', 'media.wav'), ('sketch', 'sketch.flsk')):
                    item = manifest.get(asset)
                    if item is not None:
                        media = path / filename
                        if item['path'] != filename or media.is_symlink() or media.stat().st_size != item['bytes'] or hash_file(media) != item['sha256']:
                            raise ValueError('media size/hash mismatch')
                self._publish(manifest, path)
            except (OSError, ValueError, KeyError, TypeError, ArchiveError) as exc:
                # Fail closed: do not reseed/reuse sequence numbers or discard bytes.
                raise RuntimeError(f'Archive recovery blocked at {path}: {exc}; preserve files and repair from backup') from exc

    def _publish(self, manifest: dict, path: Path) -> None:
        mid = manifest['message_id']
        for row in manifest['recipients']:
            if not isinstance(row['seq'], int) or row['seq'] < 1:
                raise ValueError('invalid inbox sequence')
            for old in self.index.values():
                if any(r['user_id'] == row['user_id'] and r['seq'] == row['seq'] for r in old['recipients']):
                    raise ValueError('duplicate inbox sequence')
            self.next_seq[row['user_id']] = max(self.next_seq.get(row['user_id'], 1), row['seq'] + 1)
        self.index[mid] = manifest
        self.paths[mid] = path

    def user_state(self, user: str) -> dict:
        with self.lock:
            row = self.db.execute('SELECT payload FROM user_state WHERE user_id=?', (user,)).fetchone()
            return json.loads(row[0]) if row else {}

    def save_user_state(self, user: str, payload: dict) -> None:
        with self.lock, self.db:
            self.db.execute('INSERT OR REPLACE INTO user_state VALUES (?,?)', (user, json.dumps(payload)))

    def _space(self, needed: int) -> None:
        if shutil.disk_usage(self.root).free < self.free_floor + needed + BLOCK:
            raise ArchiveError(507, 'insufficient storage')

    def _copy(self, source: BinaryIO, dest: Path, limit: int) -> dict:
        self._space(limit)
        count = 0
        digest = hashlib.sha256()
        with dest.open('wb') as out:
            while block := source.read(BLOCK):
                count += len(block)
                if count > limit:
                    raise ArchiveError(413, 'media too large')
                out.write(block)
                digest.update(block)
            out.flush()
            os.fsync(out.fileno())
        if not count:
            raise ArchiveError(400, 'empty media')
        return {'path': dest.name, 'bytes': count, 'sha256': digest.hexdigest()}

    def _commit(self, stage: Path, metadata: dict, audio: dict, sketch: dict | None,
                duration: int, *, completion: dict | None = None) -> dict:
        manifest = json.loads(json.dumps(metadata))
        manifest.update(schema='family-product-manifest/1', state='complete',
                        duration_ms=duration, audio=audio, sketch=sketch,
                        completed_at=time.time(), completion=json.loads(json.dumps(completion)))
        manifest['recipients'] = [{'user_id': user, 'seq': self.next_seq.get(user, 1)} for user in metadata['targets']]
        atomic_json(stage / 'manifest.json', manifest)
        with (stage / 'complete').open('wb') as marker:
            marker.flush()
            os.fsync(marker.fileno())
        sync_dir(stage)
        crash_at('before_commit')
        date = datetime.fromtimestamp(manifest['created_at'], timezone.utc)
        parent = self.root / 'messages' / str(date.year) / f'{date.month:02d}'
        parent.mkdir(parents=True, exist_ok=True)
        sync_dir(parent.parent)
        sync_dir(parent.parent.parent)
        dest = parent / manifest['message_id']
        os.rename(stage, dest)
        crash_at('after_commit')
        self._publish(manifest, dest)
        sync_dir(stage.parent)
        sync_dir(parent)
        return manifest

    @staticmethod
    def _wav_duration(path: Path) -> int:
        # Use WAV parser, not a fixed-offset guess. Restrict the current product format.
        import wave
        try:
            with wave.open(str(path), 'rb') as wav:
                if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getcomptype()) != (1, 2, 16000, 'NONE'):
                    raise ArchiveError(400, 'audio must be 16 kHz mono 16-bit PCM WAV')
                frames = wav.getnframes()
                actual = 0
                while block := wav.readframes(BLOCK // 2):
                    actual += len(block)
                if actual != frames * 2 or frames < 16 or frames > 16000 * 180:
                    raise ArchiveError(400, 'truncated or invalid WAV duration')
                return frames * 1000 // 16000
        except (wave.Error, EOFError):
            raise ArchiveError(400, 'invalid WAV') from None

    def import_wav(self, sender: str, label: str, targets: list[str], source: BinaryIO,
                   *, sketch: BinaryIO | None = None, broadcast: bool = False,
                   system: bool = False, client_id: str | None = None) -> dict:
        if client_id is not None:
            identifier(client_id)
        if not targets:
            raise ArchiveError(400, 'no recipients')
        with self.lock:
            mid = str(uuid.uuid4())
            stage = self.root / 'incoming' / mid
            stage.mkdir()
            try:
                audio = self._copy(source, stage / 'media.wav', MAX_AUDIO)
                duration = self._wav_duration(stage / 'media.wav')
                ink = self._copy(sketch, stage / 'sketch.flsk', MAX_SKETCH) if sketch else None
                if ink:
                    from demos.server.h27_sketch.codec import unpack_sketch, SketchError
                    try:
                        blob = (stage / 'sketch.flsk').read_bytes()
                        points = unpack_sketch(blob)
                        if len(blob) != 8 + 8 * len(points) or any(p[0] > 30000 for p in points):
                            raise SketchError('invalid bounded drawing')
                    except SketchError:
                        raise ArchiveError(400, 'invalid FLSK1 sketch') from None
                    ink['format'] = 'flsk1'
                metadata = dict(message_id=mid, client_message_id=client_id, from_user=sender,
                                from_label=label, targets=sorted(set(targets)), broadcast=broadcast,
                                system=system, created_at=time.time(), protocol='v1-multipart')
                if client_id:
                    for old in self.index.values():
                        if old['from_user'] == sender and old.get('client_message_id') == client_id:
                            if any(old[key] != metadata[key] for key in ('targets', 'broadcast', 'system', 'protocol')) or old['audio']['sha256'] != audio['sha256'] or old.get('sketch') != ink:
                                raise ArchiveError(409, 'client identity conflicts with accepted message')
                            return old
                    for path in (self.root / 'incoming').glob('*/upload.json'):
                        active = json.loads(path.read_text())
                        if active['from_user'] == sender and active.get('client_message_id') == client_id:
                            raise ArchiveError(409, 'client identity belongs to an open upload')
                return self._commit(stage, metadata, audio, ink, duration)
            finally:
                if stage.exists():
                    shutil.rmtree(stage)  # Only this unacknowledged multipart staging directory.

    def create_upload(self, sender: str, label: str, targets: list[str], request: dict) -> tuple[dict, bool]:
        cid = request.get('client_message_id')
        if cid is not None:
            identifier(cid)
        with self.lock:
            candidates = list(self.index.values())
            candidates += [json.loads(p.read_text()) for p in (self.root / 'incoming').glob('*/upload.json')]
            if cid:
                for old in candidates:
                    if old['from_user'] == sender and old.get('client_message_id') == cid:
                        if old.get('create_request') != request:
                            raise ArchiveError(409, 'client identity conflicts with upload metadata')
                        return old, False
            if sum(m['from_user'] == sender and m['state'] == 'open' for m in candidates) >= 8:
                raise ArchiveError(429, 'too many open uploads')
            self._space(MAX_CHUNK)
            mid = str(uuid.uuid4())
            stage = self.root / 'incoming' / mid
            stage.mkdir()
            (stage / 'audio').mkdir()
            metadata = dict(message_id=mid, client_message_id=cid, from_user=sender, from_label=label,
                            targets=sorted(set(targets)), broadcast=request.get('broadcast', False),
                            system=False, created_at=time.time(), protocol='family-message/1',
                            state='open', create_request=request, chunks={})
            atomic_json(stage / 'upload.json', metadata)
            sync_dir(stage)
            sync_dir(stage.parent)
            return metadata, True

    def upload(self, mid: str, sender: str) -> tuple[dict, Path]:
        identifier(mid)
        if mid in self.index:
            metadata = self.index[mid]
            path = self.paths[mid]
        else:
            path = self.root / 'incoming' / mid
            try:
                metadata = json.loads((path / 'upload.json').read_text())
            except FileNotFoundError:
                raise ArchiveError(404, 'upload not found') from None
        if metadata['from_user'] != sender:
            raise ArchiveError(404, 'upload not found')
        return metadata, path

    def put_chunk(self, mid: str, sender: str, sequence: int, source: BinaryIO,
                  declared: dict) -> tuple[dict, bool]:
        if not 0 <= sequence < MAX_CHUNKS:
            raise ArchiveError(400, 'invalid chunk sequence')
        if not re.fullmatch('[0-9a-f]{64}', declared['sha256']):
            raise ArchiveError(400, 'invalid checksum')
        if not 0 < declared['duration_ms'] <= 5000 or declared['start_ms'] < 0 or declared['start_ms'] + declared['duration_ms'] > MAX_DURATION:
            raise ArchiveError(400, 'invalid chunk timing')
        if declared['bytes'] != declared['duration_ms'] * 32 or declared['bytes'] > MAX_CHUNK:
            raise ArchiveError(400, 'PCM size/duration mismatch')
        with self.lock:
            metadata, path = self.upload(mid, sender)
            if metadata['state'] != 'open':
                raise ArchiveError(409, 'upload already complete')
            old = metadata['chunks'].get(str(sequence))
            if old and old != declared:
                raise ArchiveError(409, 'chunk identity conflict')
            if not old and sum(v['bytes'] for v in metadata['chunks'].values()) + declared['bytes'] > MAX_AUDIO:
                raise ArchiveError(413, 'message too large')
            tmp = path / 'audio' / (uuid.uuid4().hex + '.part')
            try:
                actual = self._copy(source, tmp, min(MAX_CHUNK, declared['bytes']))
                if actual['bytes'] != declared['bytes'] or actual['sha256'] != declared['sha256']:
                    raise ArchiveError(400, 'chunk size/checksum mismatch')
                if old:
                    dest = path / 'audio' / f'{sequence:06d}.chunk'
                    if not dest.is_file() or hash_file(dest) != old['sha256']:
                        raise ArchiveError(409, 'stored chunk corrupt; operator repair required')
                    return old, False
                dest = path / 'audio' / f'{sequence:06d}.chunk'
                os.replace(tmp, dest)
                sync_dir(dest.parent)
                crash_at('chunk_file')
                metadata['chunks'][str(sequence)] = declared
                atomic_json(path / 'upload.json', metadata)
                crash_at('chunk_ack')
                return declared, True
            finally:
                tmp.unlink(missing_ok=True)

    def complete_upload(self, mid: str, sender: str, request: dict) -> tuple[dict, bool]:
        with self.lock:
            metadata, path = self.upload(mid, sender)
            if metadata['state'] == 'complete':
                if metadata.get('completion') != request:
                    raise ArchiveError(409, 'completion identity conflict')
                return metadata, False
            count = request['audio_chunks']
            if set(metadata['chunks']) != {str(i) for i in range(count)}:
                raise ArchiveError(409, 'missing or extra audio chunks')
            digest = hashlib.sha256()
            elapsed = 0
            chunk_paths = []
            for i in range(count):
                item = metadata['chunks'][str(i)]
                file = path / 'audio' / f'{i:06d}.chunk'
                if item['start_ms'] != elapsed or file.stat().st_size != item['bytes'] or hash_file(file) != item['sha256']:
                    raise ArchiveError(409, 'invalid or corrupt chunk timeline')
                elapsed += item['duration_ms']
                with file.open('rb') as source:
                    while block := source.read(BLOCK):
                        digest.update(block)
                chunk_paths.append(file)
            if elapsed != request['duration_ms'] or digest.hexdigest() != request['source_audio_sha256']:
                raise ArchiveError(400, 'completion duration/checksum mismatch')
            self._space(sum(v['bytes'] for v in metadata['chunks'].values()) + 44)
            final = path / 'media.wav'
            finalize_pcm_chunks_to_wav(chunk_paths, final)
            crash_at('assembled')
            audio = dict(path='media.wav', bytes=final.stat().st_size, sha256=hash_file(final))
            manifest = self._commit(path, metadata, audio, None, elapsed, completion=request)
            # Commit is canonical now. Cleanup is optional and restart-safe.
            try:
                shutil.rmtree(self.paths[mid] / 'audio')
                (self.paths[mid] / 'upload.json').unlink(missing_ok=True)
                sync_dir(self.paths[mid])
            except OSError:
                pass  # Retain recoverable duplicates; never turn a commit into failure.
            return manifest, True
