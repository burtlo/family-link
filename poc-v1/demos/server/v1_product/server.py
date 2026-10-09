"""v1 product server — hangout users, per-user inbox, web admin."""

from __future__ import annotations

import argparse
import asyncio
import logging
import time
from contextlib import asynccontextmanager
import os
import secrets
import sys
from pathlib import Path

import uvicorn
from fastapi import FastAPI, Header, Request, Query, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, RedirectResponse, FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field
from starlette.datastructures import UploadFile as StarletteUploadFile
from starlette.concurrency import run_in_threadpool

from demos.server.v1_product.archive import MessageArchive, ArchiveError, identifier

from demos.server._shared.hangout_registry import endpoint_for_token, load_registry
from demos.server._shared.registry import parse_bearer
from demos.server._shared.user_mailbox import bootstrap_mailbox
from demos.server.v1_product import ws as v1_ws

ROOT = Path(os.environ.get("FAMILY_LINK_ROOT") or Path(__file__).resolve().parents[3])


def default_data_dir() -> Path:
    if os.name == 'nt':
        return Path(os.environ.get('LOCALAPPDATA') or Path.home() / 'AppData' / 'Local') / 'Family Link'
    if sys.platform == 'darwin':
        return Path.home() / 'Library' / 'Application Support' / 'Family Link'
    return Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local' / 'share') / 'family-link'


DATA_DIR = Path(os.environ.get('FAMILY_LINK_DATA_DIR') or default_data_dir()).expanduser().resolve()
STORE_DIR = Path(os.environ.get('FAMILY_LINK_MESSAGE_STORE') or DATA_DIR / 'message_store').expanduser().resolve()
# Legacy files have insufficient metadata on their own. Never reseed over them.
if any((DATA_DIR / folder).is_dir() and any((DATA_DIR / folder).rglob('*'))
       for folder in ('blobs', 'shared', 'sketches', 'shared_sketches')):
    raise RuntimeError('Legacy media found in FAMILY_LINK_DATA_DIR. Export the running old mailbox and import into a NEW data folder before switching.')
LEGACY_DIR = ROOT / 'data' / 'v1_product'
if os.environ.get('FAMILY_LINK_START_FRESH') != '1' and not any(STORE_DIR.glob('messages/*/*/*/complete')):
    if any((LEGACY_DIR / name).is_dir() and any(p.is_file() for p in (LEGACY_DIR / name).rglob('*'))
           for name in ('blobs', 'shared', 'sketches', 'shared_sketches')):
        raise RuntimeError('Existing legacy archive detected. Preserve/export its running mailbox before switching. For an explicitly separate empty installation only, set FAMILY_LINK_START_FRESH=1; old files remain untouched.')
ARCHIVE = MessageArchive(STORE_DIR, free_floor=int(os.environ.get("FAMILY_LINK_FREE_DISK_FLOOR_BYTES", str(64 * 1024 * 1024))))
WEB_DIR = ROOT / "demos" / "parent" / "web"
BOX_WEB_DIR = ROOT / "demos" / "server" / "v1_product" / "web"
TTL_S = float(os.environ.get("FAMILY_TTL_S", str(7 * 24 * 3600)))

REGISTRY = load_registry()
v1_ws.REGISTRY = REGISTRY
MAILBOX = bootstrap_mailbox(DATA_DIR, REGISTRY, ttl_s=TTL_S, archive=ARCHIVE)
ADMIN_TOKENS: dict[str, float] = {}
ADMIN_TTL_S = 3600.0

@asynccontextmanager
async def lifespan(_app):
    logging.getLogger(__name__).info('Message archive: %s; directory fsync: %s', STORE_DIR, ARCHIVE.directory_fsync_supported)
    try:
        yield
    finally:
        ARCHIVE.close()


app = FastAPI(title="family-link v1", lifespan=lifespan)


@app.exception_handler(ArchiveError)
async def archive_error(_request: Request, exc: ArchiveError):
    return JSONResponse({'error': str(exc)}, status_code=exc.status)


@app.exception_handler(OSError)
async def disk_error(_request: Request, exc: OSError):
    logging.getLogger(__name__).exception('Storage operation failed')
    return JSONResponse({'error': 'server storage unavailable'}, status_code=507)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


class LoginBody(BaseModel):
    user_id: str
    pin: str = Field(..., min_length=1)


class ViewBody(BaseModel):
    seq: int = Field(..., ge=1)


class ReadBody(BaseModel):
    position_ms: int = Field(0, ge=0)


class AdminLoginBody(BaseModel):
    username: str
    password: str


class PinResetBody(BaseModel):
    user_id: str
    pin: str = Field(..., min_length=4, max_length=8)


class ProfileBody(BaseModel):
    avatar_slot: int | None = Field(None, ge=0, le=12)
    accent_hex: str | None = None
    autoplay_new: bool | None = None


def unauthorized() -> JSONResponse:
    return JSONResponse({"error": "unauthorized"}, status_code=401)


def require_endpoint(authorization: str | None):
    token = parse_bearer(authorization)
    if not token:
        return None
    return endpoint_for_token(token, REGISTRY)


def require_user_header(x_user_id: str | None) -> str | None:
    if not x_user_id or x_user_id not in REGISTRY.users:
        return None
    return x_user_id


def require_admin(authorization: str | None) -> bool:
    token = parse_bearer(authorization)
    if not token or token not in ADMIN_TOKENS:
        return False
    if time.time() > ADMIN_TOKENS[token]:
        ADMIN_TOKENS.pop(token, None)
        return False
    return True


async def _notify_created(created) -> None:
    async def push(msg):
        try:
            await asyncio.wait_for(v1_ws.notify_inbox(msg.to_user, msg.seq, msg.kind, msg.from_user), timeout=0.5)
        except Exception:
            logging.getLogger(__name__).exception('Notification failed after archive commit')
    # Recipient notifications are independent and never hold a committed send
    # response indefinitely behind a stalled websocket.
    await asyncio.gather(*(push(msg) for msg in created))


@app.get("/v1/hangout")
def get_hangout(authorization: str | None = Header(default=None)):
    if require_endpoint(authorization) is None:
        return unauthorized()
    users = [
        {
            "id": u.id,
            "name": u.name,
            "profile": MAILBOX.profile_dict(u.id),
        }
        for u in REGISTRY.users.values()
    ]
    return {
        "id": REGISTRY.hangout.id,
        "name": REGISTRY.hangout.name,
        "users": users,
    }


@app.post("/v1/session/login")
def session_login(
    body: LoginBody,
    authorization: str | None = Header(default=None),
):
    ep = require_endpoint(authorization)
    if ep is None:
        return unauthorized()
    if body.user_id not in REGISTRY.users:
        return JSONResponse({"error": "unknown user"}, status_code=404)
    if not MAILBOX.verify_pin(body.user_id, body.pin):
        return JSONResponse({"ok": False, "error": "wrong pin"}, status_code=401)
    pin_reset = MAILBOX.pin_was_reset(body.user_id)
    if pin_reset:
        MAILBOX.clear_pin_reset(body.user_id)
    user = REGISTRY.users[body.user_id]
    payload = MAILBOX.inbox_payload(body.user_id)
    payload.update(
        {
            "ok": True,
            "name": user.name,
            "pin_reset": pin_reset,
        }
    )
    return payload


@app.put("/v1/profile")
def put_profile(
    body: ProfileBody,
    authorization: str | None = Header(default=None),
    x_user_id: str | None = Header(default=None),
):
    if require_endpoint(authorization) is None:
        return unauthorized()
    user_id = require_user_header(x_user_id)
    if user_id is None:
        return JSONResponse({"error": "X-User-Id required"}, status_code=400)
    try:
        prof = MAILBOX.set_profile(
            user_id,
            body.avatar_slot,
            body.accent_hex,
            body.autoplay_new,
        )
    except ValueError as exc:
        return JSONResponse({"error": str(exc)}, status_code=400)
    return {"ok": True, "profile": prof}


@app.get("/v1/inbox")
def get_inbox(
    limit: int = Query(default=8, ge=1, le=16),
    before_seq: int | None = Query(default=None, ge=1),
    authorization: str | None = Header(default=None),
    x_user_id: str | None = Header(default=None),
):
    if require_endpoint(authorization) is None:
        return unauthorized()
    user_id = require_user_header(x_user_id)
    if user_id is None:
        return JSONResponse({"error": "X-User-Id required"}, status_code=400)
    return MAILBOX.inbox_payload(user_id, limit=limit, before_seq=before_seq)


@app.put("/v1/session/view")
def put_view(
    body: ViewBody,
    authorization: str | None = Header(default=None),
    x_user_id: str | None = Header(default=None),
):
    if require_endpoint(authorization) is None:
        return unauthorized()
    user_id = require_user_header(x_user_id)
    if user_id is None:
        return JSONResponse({"error": "X-User-Id required"}, status_code=400)
    MAILBOX.set_view(user_id, body.seq)
    return {"last_viewed_seq": body.seq}


@app.put("/v1/messages/{seq}/read")
def put_read(
    seq: int,
    body: ReadBody,
    authorization: str | None = Header(default=None),
    x_user_id: str | None = Header(default=None),
):
    if require_endpoint(authorization) is None:
        return unauthorized()
    user_id = require_user_header(x_user_id)
    if user_id is None:
        return JSONResponse({"error": "X-User-Id required"}, status_code=400)
    MAILBOX.mark_read(user_id, seq, body.position_ms)
    return {"seq": seq, "read": True, "position_ms": body.position_ms}


@app.put("/v1/messages/{seq}/position")
def put_position(
    seq: int,
    body: ReadBody,
    authorization: str | None = Header(default=None),
    x_user_id: str | None = Header(default=None),
):
    if require_endpoint(authorization) is None:
        return unauthorized()
    user_id = require_user_header(x_user_id)
    if user_id is None:
        return JSONResponse({"error": "X-User-Id required"}, status_code=400)
    MAILBOX.set_position(user_id, seq, body.position_ms)
    return {"seq": seq, "position_ms": body.position_ms}


@app.post("/v1/messages")
async def post_message(
    request: Request,
    authorization: str | None = Header(default=None),
    x_user_id: str | None = Header(default=None),
):
    ep = require_endpoint(authorization)
    from_user_hdr = require_user_header(x_user_id)
    if ep is None:
        return unauthorized()
    from_user = from_user_hdr
    if from_user is None:
        return JSONResponse({"error": "X-User-Id required"}, status_code=400)

    if request.headers.get('content-type', '').split(';')[0].strip() == 'application/json':
        return await create_chunk_message(request, from_user)
    form = await request.form(max_files=2, max_fields=8)
    try:
        if str(form.get('kind') or '') != 'audio':
            raise ArchiveError(400, 'kind must be audio')
        to_user = form.get('to_user_id')
        broadcast = str(form.get('broadcast') or '').lower() in {'1', 'true', 'yes'}
        blob = form.get('blob')
        sketch = form.get('sketch')
        if not isinstance(blob, StarletteUploadFile):
            raise ArchiveError(400, 'blob required')
        created = await run_in_threadpool(
            MAILBOX.post_audio_stream, from_user, to_user_id=str(to_user) if to_user else None,
            broadcast=broadcast, source=blob.file,
            sketch=sketch.file if isinstance(sketch, StarletteUploadFile) and sketch.size else None,
            client_id=str(form['client_message_id']) if form.get('client_message_id') else None)
    except ValueError as exc:
        raise ArchiveError(400, str(exc)) from exc
    finally:
        await form.close()

    await _notify_created(created)
    return {
        "messages": [
            {
                "seq": m.seq,
                "kind": m.kind,
                "from": m.from_user,
                "to": m.to_user,
                "broadcast_id": m.broadcast_id,
                "message_id": m.message_id,
            }
            for m in created
        ]
    }


@app.get('/v1/outgoing/{client_message_id}')
def outgoing_receipt(client_message_id: str,
                     authorization: str | None = Header(default=None),
                     x_user_id: str | None = Header(default=None)):
    if require_endpoint(authorization) is None:
        return unauthorized()
    user = require_user_header(x_user_id)
    if user is None:
        raise ArchiveError(400, 'X-User-Id required')
    identifier(client_message_id)
    with ARCHIVE.lock:
        manifest = next((m for m in ARCHIVE.index.values()
                         if m['from_user'] == user and m.get('client_message_id') == client_message_id), None)
        if manifest is None:
            raise ArchiveError(404, 'no committed receipt')
        return dict(schema='family-send-receipt/1', client_message_id=client_message_id,
                    message_id=manifest['message_id'], state='complete', from_user_id=user,
                    recipients=manifest['recipients'], audio=manifest['audio'], sketch=manifest.get('sketch'),
                    duration_ms=manifest['duration_ms'], power_loss_qualified=False)


@app.get("/v1/messages/{seq}/blob")
def get_blob(
    seq: int,
    authorization: str | None = Header(default=None),
    x_user_id: str | None = Header(default=None),
):
    if require_endpoint(authorization) is None:
        return unauthorized()
    user_id = require_user_header(x_user_id)
    if user_id is None:
        return JSONResponse({"error": "X-User-Id required"}, status_code=400)
    path = MAILBOX.media_path(user_id, seq)
    if path is None:
        return JSONResponse({"error": "not found"}, status_code=404)
    return FileResponse(path, media_type="audio/wav")


@app.get("/v1/messages/{seq}/sketch")
def get_sketch(
    seq: int,
    authorization: str | None = Header(default=None),
    x_user_id: str | None = Header(default=None),
):
    if require_endpoint(authorization) is None:
        return unauthorized()
    user_id = require_user_header(x_user_id)
    if user_id is None:
        return JSONResponse({"error": "X-User-Id required"}, status_code=400)
    path = MAILBOX.media_path(user_id, seq, sketch=True)
    if path is None:
        return JSONResponse({"error": "not found"}, status_code=404)
    return FileResponse(path, media_type="application/octet-stream")


@app.post("/v1/admin/login")
def admin_login(body: AdminLoginBody):
    for user in REGISTRY.users.values():
        if not user.web_admin:
            continue
        if body.username != user.id and body.username != user.name.lower():
            continue
        if body.password != user.web_password:
            return JSONResponse({"error": "wrong password"}, status_code=401)
        token = secrets.token_urlsafe(24)
        ADMIN_TOKENS[token] = time.time() + ADMIN_TTL_S
        return {"ok": True, "token": token, "user_id": user.id}
    return JSONResponse({"error": "unknown admin"}, status_code=401)


@app.post("/v1/admin/pin-reset")
def admin_pin_reset(
    body: PinResetBody,
    authorization: str | None = Header(default=None),
):
    if not require_admin(authorization):
        return unauthorized()
    if body.user_id not in REGISTRY.users:
        return JSONResponse({"error": "unknown user"}, status_code=404)
    MAILBOX.reset_pin_flag(body.user_id, body.pin)
    return {"ok": True, "user_id": body.user_id, "pin": body.pin}


@app.post("/v1/admin/welcome")
async def admin_welcome(
    request: Request,
    authorization: str | None = Header(default=None),
):
    if not require_admin(authorization):
        return unauthorized()
    form = await request.form(max_files=1, max_fields=2)
    try:
        blob = form.get('blob')
        if not isinstance(blob, StarletteUploadFile):
            raise ArchiveError(400, 'blob required')
        manifest = await run_in_threadpool(ARCHIVE.import_wav, 'system', REGISTRY.hangout.name,
                                           list(REGISTRY.users), blob.file, system=True)
        created = MAILBOX.publish_manifest(manifest)
    finally:
        await form.close()
    await _notify_created(created)
    return {'ok': True, 'seeded': [{'user_id': m.to_user, 'seq': m.seq} for m in created]}



@app.post("/v1/admin/messages")
async def admin_send_message(
    request: Request,
    authorization: str | None = Header(default=None),
):
    if not require_admin(authorization):
        return unauthorized()
    admin_user = next(
        (u for u in REGISTRY.users.values() if u.web_admin), None
    )
    if admin_user is None:
        return JSONResponse({"error": "no admin user"}, status_code=500)
    from_user = admin_user.id

    form = await request.form(max_files=2, max_fields=8)
    try:
        if str(form.get('kind') or '') != 'audio':
            raise ArchiveError(400, 'kind must be audio')
        blob = form.get('blob')
        sketch = form.get('sketch')
        if not isinstance(blob, StarletteUploadFile):
            raise ArchiveError(400, 'blob required')
        to_user = form.get('to_user_id')
        created = await run_in_threadpool(
            MAILBOX.post_audio_stream, from_user,
            to_user_id=str(to_user) if to_user else None,
            broadcast=str(form.get('broadcast') or '').lower() in {'1', 'true', 'yes'},
            source=blob.file, sketch=sketch.file if isinstance(sketch, StarletteUploadFile) and sketch.size else None,
            client_id=str(form['client_message_id']) if form.get('client_message_id') else None)
    except ValueError as exc:
        raise ArchiveError(400, str(exc)) from exc
    finally:
        await form.close()

    await _notify_created(created)
    return {
        "messages": [
            {"seq": m.seq, "to": m.to_user, "from": m.from_user}
            for m in created
        ]
    }


from demos.server.v1_product.chunk_api import attach_product_chunks
from demos.server.v1_product.body_limit import UploadLimit

app.add_middleware(UploadLimit)
create_chunk_message = attach_product_chunks(app, ARCHIVE, MAILBOX, REGISTRY,
                                             require_endpoint, require_user_header, _notify_created)


@app.websocket("/v1/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await websocket.accept()
    endpoint_id: str | None = None
    try:
        try:
            raw = await websocket.receive_text()
        except WebSocketDisconnect:
            return
        body = v1_ws.parse_hello(raw)
        if body is None:
            await v1_ws.reject_hello(websocket)
            return
        endpoint = v1_ws.endpoint_from_hello(body)
        if endpoint is None:
            await v1_ws.reject_hello(websocket)
            return
        endpoint_id = endpoint.id
        await v1_ws.attach(endpoint_id, websocket)
        await websocket.send_json({"type": "hello_ok", "endpoint_id": endpoint_id})
        while True:
            message = await websocket.receive()
            if message["type"] == "websocket.disconnect":
                break
    except WebSocketDisconnect:
        pass
    finally:
        await v1_ws.drop(endpoint_id, websocket)


@app.get("/")
def index():
    return RedirectResponse(url="/box/")


def _mount_static() -> None:
    if WEB_DIR.is_dir():
        app.mount("/app", StaticFiles(directory=str(WEB_DIR), html=True), name="parent")
    if BOX_WEB_DIR.is_dir():
        app.mount("/box", StaticFiles(directory=str(BOX_WEB_DIR), html=True), name="box")


_mount_static()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--ssl-certfile", default=None)
    parser.add_argument("--ssl-keyfile", default=None)
    args = parser.parse_args()
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    kwargs: dict = {}
    if args.ssl_certfile or args.ssl_keyfile:
        if not args.ssl_certfile or not args.ssl_keyfile:
            parser.error("both --ssl-certfile and --ssl-keyfile are required")
        kwargs["ssl_certfile"] = args.ssl_certfile
        kwargs["ssl_keyfile"] = args.ssl_keyfile
    uvicorn.run(app, host=args.host, port=args.port, **kwargs)


if __name__ == "__main__":
    main()
