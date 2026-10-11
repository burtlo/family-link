#!/usr/bin/env python3
"""LAB-02 Phase B paging fixture (fl.inbox-page/2 subset)."""

from __future__ import annotations

import argparse
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_FIXTURE = ROOT / "client/labs/lab-02-bounded-inbox/fixtures/inbox_64.json"


class InboxState:
    def __init__(self, items: list[dict[str, Any]]) -> None:
        self._lock = threading.Lock()
        self.inbox_revision = 1
        self.items = list(items)
        self._delay_ms = 0

    def reset(self, items: list[dict[str, Any]]) -> None:
        with self._lock:
            self.items = list(items)
            self.inbox_revision = 1
            self._delay_ms = 0

    def set_delay(self, ms: int) -> None:
        with self._lock:
            self._delay_ms = max(0, ms)

    def push_item(self, sender_name: str, preview: str) -> None:
        with self._lock:
            idx = len(self.items)
            self.items.append(
                {
                    "message_id": f"msg-push-{idx:04d}",
                    "sender_id": f"lab-push-{idx}",
                    "sender_name": sender_name or "Lab",
                    "preview": preview or "Pushed item",
                    "duration_ms": 2000 + (idx % 7) * 137,
                    "seq": idx + 1,
                    "unread": True,
                }
            )
            self.inbox_revision += 1

    def remove_at(self, index: int) -> bool:
        with self._lock:
            if index < 0 or index >= len(self.items):
                return False
            del self.items[index]
            for i, it in enumerate(self.items):
                it["seq"] = i + 1
            self.inbox_revision += 1
            return True

    def page(self, cursor: int, limit: int) -> dict[str, Any]:
        with self._lock:
            delay = self._delay_ms
            self._delay_ms = 0
            if delay > 0:
                time.sleep(delay / 1000.0)
            total = len(self.items)
            cursor = max(0, cursor)
            limit = max(1, min(limit, 32))
            slice_items = self.items[cursor : cursor + limit]
            next_cursor = cursor + len(slice_items)
            has_more = next_cursor < total
            v2_items = []
            unread = 0
            for it in slice_items:
                if it.get("unread"):
                    unread += 1
                v2_items.append(
                    {
                        "message_id": it["message_id"],
                        "recipient_sequence": it.get("seq", 0),
                        "sender_user_id": it.get("sender_id", "user"),
                        "sender_name": it.get("sender_name", "?"),
                        "received_at": "2026-10-11T18:00:00Z",
                        "kind": "audio",
                        "read": not it.get("unread", False),
                        "position_ms": 0,
                        "duration_ms": int(it.get("duration_ms", 0)),
                        "preview": it.get("preview", ""),
                    }
                )
            return {
                "schema": "fl.inbox-page/2",
                "inbox_revision": self.inbox_revision,
                "total_count": total,
                "unread_count": sum(1 for x in self.items if x.get("unread")),
                "last_viewed_message_id": self.items[0]["message_id"] if self.items else None,
                "items": v2_items,
                "next_cursor": str(next_cursor) if has_more else None,
                "has_more": has_more,
                "page_offset": cursor,
            }


def load_v1_fixture(path: Path) -> list[dict[str, Any]]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    items = raw.get("items") or []
    if not isinstance(items, list):
        raise ValueError("fixture items must be a list")
    return items


STATE: InboxState
SEED_ITEMS: list[dict[str, Any]]


class Handler(BaseHTTPRequestHandler):
    server_version = "lab02-paging/1"

    def log_message(self, fmt: str, *args: Any) -> None:
        print(f"[lab02-paging] {self.address_string()} - {fmt % args}")

    def _json_response(self, code: int, body: dict[str, Any]) -> None:
        data = json.dumps(body).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        if parsed.path == "/lab/health":
            self._json_response(200, {"ok": True, "lab": "LAB-02", "schema": "fl.inbox-page/2"})
            return
        if parsed.path != "/v2/inbox":
            self._json_response(404, {"error": "not_found"})
            return
        qs = parse_qs(parsed.query)
        cursor = int((qs.get("cursor") or ["0"])[0])
        limit = int((qs.get("limit") or ["16"])[0])
        self._json_response(200, STATE.page(cursor, limit))

    def do_POST(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        if parsed.path != "/lab/script":
            self._json_response(404, {"error": "not_found"})
            return
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length) or b"{}")
        name = str(body.get("name", ""))
        if name == "delay_ms":
            STATE.set_delay(int(body.get("ms", 500)))
            self._json_response(200, {"ok": True, "script": name})
            return
        if name == "push_item":
            STATE.push_item(str(body.get("sender_name", "Lab")), str(body.get("preview", "")))
            self._json_response(200, {"ok": True, "total": len(STATE.items)})
            return
        if name == "remove_at":
            ok = STATE.remove_at(int(body.get("index", -1)))
            self._json_response(200 if ok else 400, {"ok": ok, "total": len(STATE.items)})
            return
        if name == "reset":
            STATE.reset(SEED_ITEMS)
            self._json_response(200, {"ok": True, "total": len(STATE.items)})
            return
        self._json_response(400, {"error": "unknown_script", "name": name})


def main() -> int:
    global STATE, SEED_ITEMS
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--fixture", type=Path, default=DEFAULT_FIXTURE)
    args = ap.parse_args()
    SEED_ITEMS = load_v1_fixture(args.fixture.resolve())
    STATE = InboxState(SEED_ITEMS)
    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"LAB02_PAGING_SERVER listening http://{args.host}:{args.port} items={len(SEED_ITEMS)}")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
