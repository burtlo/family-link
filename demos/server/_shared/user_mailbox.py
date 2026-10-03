"""Per-user inbox, session state, broadcast fan-out, First Message seed."""

from __future__ import annotations

import time
import uuid
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

from demos.server._shared.hangout_registry import HangoutRegistry, load_registry


@dataclass
class Message:
    seq: int
    kind: str
    from_user: str
    from_label: str
    to_user: str
    created_at: float
    system: bool = False
    broadcast_id: str | None = None
    has_blob: bool = False
    has_sketch: bool = False
    duration_ms: int | None = None


@dataclass
class UserProfile:
    avatar_slot: int = 0
    accent_hex: str | None = None
    autoplay_new: bool = False


@dataclass
class UserSession:
    last_viewed_seq: int = 0
    read: set[int] = field(default_factory=set)
    position_ms: dict[int, int] = field(default_factory=dict)
    pin_reset: bool = False


class UserMailbox:
    def __init__(self, registry: HangoutRegistry, data_dir: Path, ttl_s: float = 3600.0):
        self.registry = registry
        self.data_dir = data_dir
        self.ttl_s = ttl_s
        self.inboxes: dict[str, list[Message]] = defaultdict(list)
        self.next_seq: dict[str, int] = defaultdict(lambda: 1)
        self.sessions: dict[str, UserSession] = defaultdict(UserSession)
        self.profiles: dict[str, UserProfile] = defaultdict(UserProfile)
        self._pin_overrides: dict[str, str] = {}
        self._blob_dir = data_dir / "blobs"
        self._shared_dir = data_dir / "shared"
        self._sketch_dir = data_dir / "sketches"
        self._shared_sketch_dir = data_dir / "shared_sketches"
        self._blob_dir.mkdir(parents=True, exist_ok=True)
        self._shared_dir.mkdir(parents=True, exist_ok=True)
        self._sketch_dir.mkdir(parents=True, exist_ok=True)
        self._shared_sketch_dir.mkdir(parents=True, exist_ok=True)

    def _session(self, user_id: str) -> UserSession:
        return self.sessions[user_id]

    def alive(self, msg: Message) -> bool:
        if msg.system:
            return True
        return (time.time() - msg.created_at) < self.ttl_s

    def sorted_inbox(self, user_id: str) -> list[Message]:
        return sorted(
            [m for m in self.inboxes[user_id] if self.alive(m)],
            key=lambda m: m.seq,
        )

    def unread_count(self, user_id: str) -> int:
        sess = self._session(user_id)
        return sum(1 for m in self.sorted_inbox(user_id) if m.seq not in sess.read)

    def public_message(self, msg: Message, user_id: str) -> dict:
        sess = self._session(user_id)
        item = {
            "seq": msg.seq,
            "kind": msg.kind,
            "from": msg.from_user,
            "from_label": msg.from_label,
            "system": msg.system,
            "read": msg.seq in sess.read,
            "position_ms": sess.position_ms.get(msg.seq, 0),
            "created_at": msg.created_at,
        }
        if msg.broadcast_id:
            item["broadcast_id"] = msg.broadcast_id
        if msg.duration_ms is not None:
            item["duration_ms"] = msg.duration_ms
        item["has_sketch"] = msg.has_sketch
        return item

    def _default_accent(self, user_id: str) -> str:
        hues = [
            "#5AA0E8",
            "#E8C040",
            "#7AC47A",
            "#C070E8",
            "#E87A9A",
            "#7AD4E8",
            "#D4A0E8",
            "#E8A87A",
            "#4ECDC4",
            "#FF6B6B",
        ]
        ids = list(self.registry.users)
        try:
            idx = ids.index(user_id)
        except ValueError:
            idx = 0
        return hues[idx % len(hues)]

    def profile_dict(self, user_id: str) -> dict:
        prof = self.profiles[user_id]
        accent = prof.accent_hex or self._default_accent(user_id)
        slot = prof.avatar_slot
        if slot < 0:
            slot = 0
        if slot > 12:
            slot = 12
        return {
            "avatar_slot": slot,
            "accent_hex": accent,
            "autoplay_new": prof.autoplay_new,
        }

    def set_profile(
        self,
        user_id: str,
        avatar_slot: int | None = None,
        accent_hex: str | None = None,
        autoplay_new: bool | None = None,
    ) -> dict:
        if user_id not in self.registry.users:
            raise ValueError("unknown user")
        prof = self.profiles[user_id]
        if avatar_slot is not None:
            if avatar_slot < 0 or avatar_slot > 12:
                raise ValueError("avatar_slot must be 0..12")
            prof.avatar_slot = avatar_slot
        if accent_hex is not None:
            if accent_hex and not accent_hex.startswith("#"):
                accent_hex = f"#{accent_hex}"
            if accent_hex:
                prof.accent_hex = accent_hex.upper()
        if autoplay_new is not None:
            prof.autoplay_new = autoplay_new
        return self.profile_dict(user_id)

    def inbox_payload(self, user_id: str) -> dict:
        msgs = self.sorted_inbox(user_id)
        sess = self._session(user_id)
        if not msgs:
            view = 0
        elif sess.last_viewed_seq <= 0:
            view = msgs[-1].seq
        else:
            view = sess.last_viewed_seq
        return {
            "user_id": user_id,
            "last_viewed_seq": view,
            "unread": self.unread_count(user_id),
            "messages": [self.public_message(m, user_id) for m in msgs],
            "profile": self.profile_dict(user_id),
        }

    def _label_for(self, user_id: str) -> str:
        u = self.registry.users.get(user_id)
        return u.name if u else user_id

    def _store_blob(self, user_id: str, seq: int, data: bytes) -> None:
        dest = self._blob_dir / user_id / str(seq)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)

    def _shared_blob_path(self, blob_id: str) -> Path:
        return self._shared_dir / blob_id

    def _shared_sketch_path(self, blob_id: str) -> Path:
        return self._shared_sketch_dir / f"{blob_id}.flsk"

    def store_shared_blob(self, blob_id: str, data: bytes) -> None:
        self._shared_blob_path(blob_id).write_bytes(data)

    def _store_sketch(self, user_id: str, seq: int, data: bytes) -> None:
        dest = self._sketch_dir / user_id / f"{seq}.flsk"
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)

    def store_shared_sketch(self, blob_id: str, data: bytes) -> None:
        self._shared_sketch_path(blob_id).write_bytes(data)

    def read_blob(self, user_id: str, seq: int) -> bytes | None:
        for msg in self.inboxes[user_id]:
            if msg.seq != seq or not msg.has_blob:
                continue
            if msg.broadcast_id:
                path = self._shared_blob_path(msg.broadcast_id)
            else:
                path = self._blob_dir / user_id / str(seq)
            if path.is_file():
                return path.read_bytes()
        return None

    def _append_message(
        self,
        to_user: str,
        *,
        kind: str,
        from_user: str,
        from_label: str,
        system: bool = False,
        broadcast_id: str | None = None,
        blob: bytes | None = None,
        sketch: bytes | None = None,
        duration_ms: int | None = None,
    ) -> Message:
        seq = self.next_seq[to_user]
        self.next_seq[to_user] = seq + 1
        msg = Message(
            seq=seq,
            kind=kind,
            from_user=from_user,
            from_label=from_label,
            to_user=to_user,
            created_at=time.time(),
            system=system,
            broadcast_id=broadcast_id,
            has_blob=blob is not None,
            has_sketch=sketch is not None,
            duration_ms=duration_ms,
        )
        if blob is not None:
            if broadcast_id:
                if not self._shared_blob_path(broadcast_id).is_file():
                    self.store_shared_blob(broadcast_id, blob)
            else:
                self._store_blob(to_user, seq, blob)
        if sketch is not None:
            if broadcast_id:
                if not self._shared_sketch_path(broadcast_id).is_file():
                    self.store_shared_sketch(broadcast_id, sketch)
            else:
                self._store_sketch(to_user, seq, sketch)
        self.inboxes[to_user].append(msg)
        return msg

    def post_audio(
        self,
        from_user: str,
        *,
        to_user_id: str | None = None,
        broadcast: bool = False,
        blob: bytes,
        sketch: bytes | None = None,
        duration_ms: int | None = None,
    ) -> list[Message]:
        if broadcast:
            targets = [
                uid
                for uid in self.registry.users
                if uid != from_user
            ]
            bid = str(uuid.uuid4())
            out: list[Message] = []
            for to_user in targets:
                out.append(
                    self._append_message(
                        to_user,
                        kind="audio",
                        from_user=from_user,
                        from_label=self._label_for(from_user),
                        broadcast_id=bid,
                        blob=blob,
                        sketch=sketch,
                        duration_ms=duration_ms,
                    )
                )
            return out
        if not to_user_id or to_user_id not in self.registry.users:
            raise ValueError("invalid to_user_id")
        return [
            self._append_message(
                to_user_id,
                kind="audio",
                from_user=from_user,
                from_label=self._label_for(from_user),
                blob=blob,
                sketch=sketch,
                duration_ms=duration_ms,
            )
        ]

    def read_sketch(self, user_id: str, seq: int) -> bytes | None:
        for msg in self.inboxes[user_id]:
            if msg.seq != seq or not msg.has_sketch:
                continue
            if msg.broadcast_id:
                path = self._shared_sketch_path(msg.broadcast_id)
            else:
                path = self._sketch_dir / user_id / f"{seq}.flsk"
            if path.is_file():
                return path.read_bytes()
        return None

    def seed_first_message(self, wav: bytes, duration_ms: int | None = None) -> None:
        label = self.registry.hangout.name
        for uid in self.registry.users:
            if self.next_seq[uid] > 1:
                continue
            self._append_message(
                uid,
                kind="audio",
                from_user="system",
                from_label=label,
                system=True,
                blob=wav,
                duration_ms=duration_ms,
            )
            sess = self._session(uid)
            if sess.last_viewed_seq <= 0:
                sess.last_viewed_seq = 1

    def set_view(self, user_id: str, seq: int) -> None:
        self._session(user_id).last_viewed_seq = seq

    def mark_read(self, user_id: str, seq: int, position_ms: int = 0) -> None:
        sess = self._session(user_id)
        sess.read.add(seq)
        sess.position_ms[seq] = position_ms

    def set_position(self, user_id: str, seq: int, position_ms: int) -> None:
        self._session(user_id).position_ms[seq] = position_ms

    def reset_pin_flag(self, user_id: str, new_pin: str) -> None:
        user = self.registry.users.get(user_id)
        if user is None:
            raise ValueError("unknown user")
        # Registry is frozen; mutability lives in a side table on the server.
        self._session(user_id).pin_reset = True
        self._pin_overrides[user_id] = new_pin

    def verify_pin(self, user_id: str, pin: str) -> bool:
        if user_id in self._pin_overrides:
            return self._pin_overrides[user_id] == pin
        user = self.registry.users.get(user_id)
        if user is None:
            return False
        return user.pin == pin

    def pin_was_reset(self, user_id: str) -> bool:
        return self._session(user_id).pin_reset

    def clear_pin_reset(self, user_id: str) -> None:
        self._session(user_id).pin_reset = False

    def append_system_welcome(self, wav: bytes, duration_ms: int | None) -> list[dict]:
        created = []
        for uid in self.registry.users:
            msg = self._append_message(
                uid,
                kind="audio",
                from_user="system",
                from_label=self.registry.hangout.name,
                system=True,
                blob=wav,
                duration_ms=duration_ms,
            )
            created.append({"user_id": uid, "seq": msg.seq})
        return created


def wav_duration_ms(data: bytes) -> int | None:
    if len(data) < 44 or data[:4] != b"RIFF":
        return None
    rate = int.from_bytes(data[24:28], "little")
    bits = int.from_bytes(data[34:36], "little")
    channels = int.from_bytes(data[22:24], "little")
    if rate <= 0 or bits <= 0 or channels <= 0:
        return None
    data_bytes = int.from_bytes(data[40:44], "little")
    bytes_per_sec = rate * channels * (bits // 8)
    if bytes_per_sec <= 0:
        return None
    return int(data_bytes * 1000 / bytes_per_sec)


def minimal_wav(seconds: float = 1.0, rate: int = 16000) -> bytes:
    """Short tone for First Message when no welcome_wav is configured."""
    import math
    import struct

    n = int(rate * seconds)
    samples = []
    for i in range(n):
        v = int(8000 * math.sin(2 * math.pi * 440 * i / rate))
        samples.append(struct.pack("<h", v))
    pcm = b"".join(samples)
    data_size = len(pcm)
    riff_size = 36 + data_size
    header = struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF",
        riff_size,
        b"WAVE",
        b"fmt ",
        16,
        1,
        1,
        rate,
        rate * 2,
        2,
        16,
        b"data",
        data_size,
    )
    return header + pcm


def bootstrap_mailbox(
    data_dir: Path, registry: HangoutRegistry | None = None, ttl_s: float = 3600.0
) -> UserMailbox:
    reg = registry or load_registry()
    box = UserMailbox(reg, data_dir, ttl_s=ttl_s)
    wav: bytes | None = None
    if reg.welcome_wav and reg.welcome_wav.is_file():
        wav = reg.welcome_wav.read_bytes()
    if wav is None:
        wav = minimal_wav(1.5)
    box.seed_first_message(wav, wav_duration_ms(wav))
    return box
