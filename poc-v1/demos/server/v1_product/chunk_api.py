"""PCM subset of family-message/1 on the actual product authentication model."""
from __future__ import annotations

import asyncio
import json
import tempfile

from fastapi import Header, Request
from fastapi.responses import FileResponse, JSONResponse
from pydantic import BaseModel, ConfigDict, Field, ValidationError
from starlette.concurrency import run_in_threadpool

from demos.server.v1_product.archive import ArchiveError, MAX_CHUNK, MAX_CHUNKS, MAX_DURATION


class StrictBody(BaseModel):
    model_config = ConfigDict(extra='forbid', strict=True)


class AudioMeta(StrictBody):
    codec: str
    sample_rate_hz: int = 16000
    channels: int = 1
    target_chunk_ms: int = Field(2000, ge=1, le=5000)


class CreateBody(StrictBody):
    protocol: str = 'family-message/1'
    client_message_id: str | None = None
    to_user_id: str | None = None
    broadcast: bool = False
    audio: AudioMeta


class CompleteBody(StrictBody):
    audio_chunks: int = Field(ge=1, le=MAX_CHUNKS)
    duration_ms: int = Field(ge=1, le=MAX_DURATION)
    source_audio_sha256: str = Field(pattern='^[0-9a-f]{64}$')
    sketch_sequences: list[int] = Field(default_factory=list)
    closed_reason: str = 'button'


async def parse_json(request: Request, model):
    data = bytearray()
    async for block in request.stream():
        if len(data) + len(block) > 4096:
            raise ArchiveError(413, 'JSON body too large')
        data.extend(block)
    try:
        return model.model_validate(json.loads(data))
    except (ValidationError, ValueError, TypeError):
        raise ArchiveError(400, 'malformed or unsupported message metadata') from None


def attach_product_chunks(app, archive, mailbox, registry, require_endpoint, require_user, notify):
    transfers = asyncio.Semaphore(2)

    def sender(authorization, user):
        if require_endpoint(authorization) is None:
            raise ArchiveError(401, 'unauthorized')
        uid = require_user(user)
        if uid is None:
            raise ArchiveError(400, 'X-User-Id required')
        return uid

    async def create(request: Request, uid: str):
        body = await parse_json(request, CreateBody)
        if body.protocol != 'family-message/1' or body.audio.codec != 'pcm_s16le' or body.audio.sample_rate_hz != 16000 or body.audio.channels != 1:
            raise ArchiveError(400, 'only family-message/1 16 kHz mono PCM chunks are currently supported')
        if body.broadcast == (body.to_user_id is not None):
            raise ArchiveError(400, 'choose recipient or broadcast')
        targets = [u for u in registry.users if u != uid] if body.broadcast else [body.to_user_id]
        if not targets or any(u not in registry.users for u in targets):
            raise ArchiveError(400, 'invalid recipient')
        metadata, new = await run_in_threadpool(archive.create_upload, uid, registry.users[uid].name,
                                                 targets, body.model_dump())
        return JSONResponse(dict(message_id=metadata['message_id'], client_message_id=metadata.get('client_message_id'),
                                 state=metadata['state'], limits=dict(max_duration_ms=MAX_DURATION,
                                 max_audio_chunk_bytes=MAX_CHUNK, max_audio_bytes=6*1024*1024,
                                 max_audio_chunks=MAX_CHUNKS, sketch_supported=False)), status_code=201 if new else 200)

    @app.put('/v1/messages/{message_id}/audio/{sequence}')
    async def put_audio(message_id: str, sequence: int, request: Request,
                        authorization: str | None = Header(default=None),
                        x_user_id: str | None = Header(default=None)):
        uid = sender(authorization, x_user_id)
        # Authenticate ownership before spooling request bytes.
        await run_in_threadpool(archive.upload, message_id, uid)
        if request.headers.get('content-type', '').split(';')[0] != 'application/octet-stream':
            raise ArchiveError(400, 'chunk content type must be application/octet-stream')
        try:
            declared = dict(sha256=request.headers['x-chunk-sha256'], bytes=int(request.headers['x-chunk-bytes']),
                            start_ms=int(request.headers['x-chunk-start-ms']), duration_ms=int(request.headers['x-chunk-duration-ms']))
        except (KeyError, ValueError):
            raise ArchiveError(400, 'required chunk headers missing or invalid') from None
        if not 0 < declared['bytes'] <= MAX_CHUNK:
            raise ArchiveError(413, 'invalid chunk size')
        async with transfers:
            with tempfile.TemporaryFile(dir=archive.root / 'state') as spool:
                size = 0
                async for block in request.stream():
                    size += len(block)
                    if size > declared['bytes']:
                        raise ArchiveError(413, 'chunk too large')
                    await run_in_threadpool(archive._space, len(block))
                    await run_in_threadpool(spool.write, block)
                spool.seek(0)
                item, new = await run_in_threadpool(archive.put_chunk, message_id, uid, sequence, spool, declared)
        return JSONResponse(dict(message_id=message_id, sequence=sequence, sha256=item['sha256'],
                                 bytes=item['bytes'], durable=True,
                                 directory_fsync_supported=archive.directory_fsync_supported,
                                 power_loss_qualified=False), status_code=201 if new else 200)

    @app.get('/v1/messages/{message_id}/upload')
    def status(message_id: str, authorization: str | None = Header(default=None),
               x_user_id: str | None = Header(default=None)):
        uid = sender(authorization, x_user_id)
        with archive.lock:
            metadata, _ = archive.upload(message_id, uid)
            chunks = metadata.get('chunks', {})
            return dict(message_id=message_id, state=metadata['state'],
                        audio=dict(received=sorted(int(i) for i in chunks), bytes=sum(v['bytes'] for v in chunks.values())),
                        sketch=dict(received=[], bytes=0), recipients=metadata.get('recipients', []))

    @app.post('/v1/messages/{message_id}/complete')
    async def complete(message_id: str, request: Request,
                       authorization: str | None = Header(default=None),
                       x_user_id: str | None = Header(default=None)):
        uid = sender(authorization, x_user_id)
        body = await parse_json(request, CompleteBody)
        if body.sketch_sequences or body.closed_reason not in {'button', 'silence', 'duration_limit', 'recovered'}:
            raise ArchiveError(400, 'unsupported sketch or closed reason')
        metadata, new = await run_in_threadpool(archive.complete_upload, message_id, uid, body.model_dump())
        created = mailbox.publish_manifest(metadata)
        if new:
            await notify(created)
        return JSONResponse(dict(message_id=message_id, state='complete', duration_ms=metadata['duration_ms'],
                                 recipients=metadata['recipients'], audio=metadata['audio']), status_code=201 if new else 200)

    @app.get('/v1/messages/{message_id}/audio')
    def audio(message_id: str, authorization: str | None = Header(default=None),
              x_user_id: str | None = Header(default=None)):
        uid = sender(authorization, x_user_id)
        metadata = archive.index.get(message_id)
        if metadata is None or not any(r['user_id'] == uid for r in metadata['recipients']):
            raise ArchiveError(404, 'message not found')
        return FileResponse(archive.paths[message_id] / 'media.wav', media_type='audio/wav',
                            headers={'ETag': '"' + metadata['audio']['sha256'] + '"',
                                     'X-Content-SHA256': metadata['audio']['sha256']})

    return create
