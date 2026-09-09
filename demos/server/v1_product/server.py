"""v1 product server — hangout users, per-user inbox, web admin."""

from __future__ import annotations

import argparse
import json
import os
import secrets
import time
from pathlib import Path

import uvicorn
from fastapi import FastAPI, Header, Request, UploadFile, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, Response
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field
from starlette.datastructures import UploadFile as StarletteUploadFile

from demos.server._shared.hangout_registry import endpoint_for_token, load_registry
from demos.server._shared.registry import parse_bearer
from demos.server._shared.user_mailbox import bootstrap_mailbox, wav_duration_ms
from demos.server.v1_product import ws as v1_ws

ROOT = Path(os.environ.get("FAMILY_LINK_ROOT") or Path(__file__).resolve().parents[3])
DATA_DIR = ROOT / "data" / "v1_product"
WEB_DIR = ROOT / "demos" / "parent" / "web"
BOX_WEB_DIR = ROOT / "demos" / "server" / "v1_product" / "web"
TTL_S = float(os.environ.get("FAMILY_TTL_S", str(7 * 24 * 3600)))

REGISTRY = load_registry()
MAILBOX = bootstrap_mailbox(DATA_DIR, REGISTRY, ttl_s=TTL_S)
ADMIN_TOKENS: dict[str, float] = {}
ADMIN_TTL_S = 3600.0

app = FastAPI(title="family-link v1")
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
    avatar_slot: int = Field(0, ge=0, le=12)
    accent_hex: str | None = None


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
    for msg in created:
        await v1_ws.notify_inbox(msg.to_user, msg.seq, msg.kind, msg.from_user)


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
    if require_endpoint(authorization) is None:
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
        prof = MAILBOX.set_profile(user_id, body.avatar_slot, body.accent_hex)
    except ValueError as exc:
        return JSONResponse({"error": str(exc)}, status_code=400)
    return {"ok": True, "profile": prof}


@app.get("/v1/inbox")
def get_inbox(
    authorization: str | None = Header(default=None),
    x_user_id: str | None = Header(default=None),
):
    if require_endpoint(authorization) is None:
        return unauthorized()
    user_id = require_user_header(x_user_id)
    if user_id is None:
        return JSONResponse({"error": "X-User-Id required"}, status_code=400)
    return MAILBOX.inbox_payload(user_id)


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
    if require_endpoint(authorization) is None:
        return unauthorized()
    from_user = require_user_header(x_user_id)
    if from_user is None:
        return JSONResponse({"error": "X-User-Id required"}, status_code=400)

    form = await request.form()
    try:
        kind = str(form.get("kind") or "")
        if kind != "audio":
            return JSONResponse({"error": "kind must be audio"}, status_code=400)
        to_user = form.get("to_user_id")
        to_user_id = None if to_user in (None, "") else str(to_user)
        broadcast_raw = form.get("broadcast")
        broadcast = str(broadcast_raw).lower() in {"1", "true", "yes"}
        if broadcast and to_user_id:
            return JSONResponse(
                {"error": "use to_user_id or broadcast, not both"}, status_code=400
            )
        if not broadcast and not to_user_id:
            return JSONResponse({"error": "to_user_id or broadcast required"}, status_code=400)
        blob_item = form.get("blob")
        blob_bytes: bytes | None = None
        if isinstance(blob_item, StarletteUploadFile):
            blob_bytes = await blob_item.read()
    finally:
        await form.close()

    if not blob_bytes:
        return JSONResponse({"error": "blob required"}, status_code=400)

    dur = wav_duration_ms(blob_bytes)
    try:
        created = MAILBOX.post_audio(
            from_user,
            to_user_id=to_user_id,
            broadcast=broadcast,
            blob=blob_bytes,
            duration_ms=dur,
        )
    except ValueError as exc:
        return JSONResponse({"error": str(exc)}, status_code=400)

    await _notify_created(created)
    return {
        "messages": [
            {
                "seq": m.seq,
                "kind": m.kind,
                "from": m.from_user,
                "to": m.to_user,
                "broadcast_id": m.broadcast_id,
            }
            for m in created
        ]
    }


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
    data = MAILBOX.read_blob(user_id, seq)
    if data is None:
        return JSONResponse({"error": "not found"}, status_code=404)
    return Response(data, media_type="application/octet-stream")


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
    form = await request.form()
    try:
        blob_item = form.get("blob")
        if not isinstance(blob_item, StarletteUploadFile):
            return JSONResponse({"error": "blob required"}, status_code=400)
        wav = await blob_item.read()
    finally:
        await form.close()
    if not wav:
        return JSONResponse({"error": "empty blob"}, status_code=400)
    dur = wav_duration_ms(wav)
    created = MAILBOX.append_system_welcome(wav, dur)
    for row in created:
        await v1_ws.notify_inbox(row["user_id"], row["seq"], "audio", "system")
    return {"ok": True, "seeded": created}


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

    form = await request.form()
    try:
        kind = str(form.get("kind") or "")
        if kind != "audio":
            return JSONResponse({"error": "kind must be audio"}, status_code=400)
        to_user = form.get("to_user_id")
        to_user_id = None if to_user in (None, "") else str(to_user)
        broadcast_raw = form.get("broadcast")
        broadcast = str(broadcast_raw).lower() in {"1", "true", "yes"}
        blob_item = form.get("blob")
        blob_bytes: bytes | None = None
        if isinstance(blob_item, StarletteUploadFile):
            blob_bytes = await blob_item.read()
    finally:
        await form.close()
    if not blob_bytes:
        return JSONResponse({"error": "blob required"}, status_code=400)
    dur = wav_duration_ms(blob_bytes)
    try:
        created = MAILBOX.post_audio(
            from_user,
            to_user_id=to_user_id,
            broadcast=broadcast,
            blob=blob_bytes,
            duration_ms=dur,
        )
    except ValueError as exc:
        return JSONResponse({"error": str(exc)}, status_code=400)
    await _notify_created(created)
    return {
        "messages": [
            {"seq": m.seq, "to": m.to_user, "from": m.from_user}
            for m in created
        ]
    }


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
