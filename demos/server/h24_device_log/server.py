#!/usr/bin/env python3
"""Heartbeat + device event log for firmware h24.

POST /v1/heartbeat carries optional {available, boot_id, logs[]}. Logs append
to data/h24_device_log/{device_id}.jsonl on the host (durable on the Mac for
now; the box keeps an in-RAM ring until ack).

  python -m demos.server.h24_device_log.server --host 0.0.0.0 --port 8080
  make flash DEMO=h24
"""

from __future__ import annotations

import argparse
import json
import os
import time
from dataclasses import dataclass
from pathlib import Path

import uvicorn
from fastapi import Body, FastAPI, Header, Query
from fastapi.responses import JSONResponse

from demos.server._shared.registry import device_for_token, load_devices, parse_bearer

ROOT = Path(os.environ.get("FAMILY_LINK_ROOT") or Path(__file__).resolve().parents[3])
DATA_DIR = ROOT / "data" / "h24_device_log"

STALE_S = 10.0
DEVICES = load_devices()
app = FastAPI()


@dataclass
class Presence:
    available: bool
    updated_at: float
    boot_id: int | None = None


STATE: dict[str, Presence] = {}
ACK_SEQ: dict[str, int] = {}


def unauthorized() -> JSONResponse:
    return JSONResponse({"error": "unauthorized"}, status_code=401)


def require_device(authorization: str | None):
    token = parse_bearer(authorization)
    return device_for_token(token, DEVICES) if token else None


def person(device_id: str) -> dict:
    dev = DEVICES.get(device_id)
    return {
        "id": device_id,
        "name": dev.name if dev else device_id,
    }


def peer_view(peer_id: str, now: float) -> dict:
    body = person(peer_id)
    st = STATE.get(peer_id)
    if st is None or (now - st.updated_at) >= STALE_S:
        body.update({"online": False, "available": None, "updated_at": None})
        return body
    body.update(
        {
            "online": True,
            "available": st.available,
            "updated_at": st.updated_at,
            "boot_id": st.boot_id,
        }
    )
    return body


def self_view(device_id: str, now: float) -> dict:
    body = person(device_id)
    st = STATE.get(device_id)
    if st is None:
        body.update({"online": False, "available": None, "updated_at": None})
        return body
    body.update(
        {
            "online": True,
            "available": st.available,
            "updated_at": st.updated_at,
            "boot_id": st.boot_id,
        }
    )
    return body


def append_logs(device_id: str, name: str, boot_id: int | None, rows: list[dict], now: float) -> int:
    if not rows:
        return ACK_SEQ.get(device_id, 0)
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    path = DATA_DIR / f"{device_id}.jsonl"
    ack = ACK_SEQ.get(device_id, 0)
    with path.open("a", encoding="utf-8") as fh:
        for row in rows:
            try:
                seq = int(row.get("seq", 0))
            except (TypeError, ValueError):
                continue
            if seq <= ack:
                continue
            record = {
                "device_id": device_id,
                "name": name,
                "boot_id": boot_id,
                "seq": seq,
                "uptime_ms": row.get("ms"),
                "level": row.get("lvl") or row.get("level") or "I",
                "msg": row.get("msg") or "",
                "server_time": now,
            }
            fh.write(json.dumps(record, ensure_ascii=True) + "\n")
            if seq > ack:
                ack = seq
            print(
                f"log {name} boot={boot_id} seq={seq} "
                f"+{record['uptime_ms']}ms {record['level']} {record['msg']!r}",
                flush=True,
            )
    ACK_SEQ[device_id] = ack
    return ack


@app.post("/v1/heartbeat", response_model=None)
def heartbeat(
    authorization: str | None = Header(default=None),
    body: dict | None = Body(default=None),
) -> dict | JSONResponse:
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    payload = body if isinstance(body, dict) else {}
    available = bool(payload.get("available")) if "available" in payload else False
    boot_id_raw = payload.get("boot_id")
    boot_id = int(boot_id_raw) if boot_id_raw is not None else None
    logs = payload.get("logs")
    if not isinstance(logs, list):
        logs = []
    now = time.time()
    prev = STATE.get(device.id)
    STATE[device.id] = Presence(available=available, updated_at=now, boot_id=boot_id)
    if prev is None or prev.available != available:
        print(
            f"presence {device.name} ({device.id}) available={available} boot={boot_id}",
            flush=True,
        )
    logs_ack = append_logs(device.id, device.name, boot_id, logs, now)
    return {
        "ok": True,
        "server_time": now,
        "logs_ack": logs_ack,
        "self": self_view(device.id, now),
        "peer": peer_view(device.peer, now),
    }


@app.get("/v1/logs", response_model=None)
def list_logs(
    authorization: str | None = Header(default=None),
    device_id: str | None = Query(default=None),
    tail: int = Query(default=20, ge=1, le=200),
) -> dict | JSONResponse:
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    target = device_id or device.id
    path = DATA_DIR / f"{target}.jsonl"
    if not path.is_file():
        return {"device_id": target, "lines": [], "logs_ack": ACK_SEQ.get(target, 0)}
    lines: list[dict] = []
    with path.open(encoding="utf-8") as fh:
        for raw in fh:
            raw = raw.strip()
            if not raw:
                continue
            try:
                lines.append(json.loads(raw))
            except json.JSONDecodeError:
                continue
    return {
        "device_id": target,
        "lines": lines[-tail:],
        "logs_ack": ACK_SEQ.get(target, 0),
    }


@app.get("/v1/me", response_model=None)
def me(authorization: str | None = Header(default=None)) -> dict | JSONResponse:
    device = require_device(authorization)
    if device is None:
        return unauthorized()
    now = time.time()
    return {
        "device_id": device.id,
        "name": device.name,
        "peer_id": device.peer,
        "logs_ack": ACK_SEQ.get(device.id, 0),
        "self": self_view(device.id, now),
        "peer": peer_view(device.peer, now),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
