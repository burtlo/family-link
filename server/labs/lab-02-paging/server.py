#!/usr/bin/env python3
"""LAB-02 Phase B paging fixture (fl.inbox-page/2 subset)."""

from __future__ import annotations

import argparse
import json
import hashlib
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_FIXTURE = ROOT / "client/labs/lab-02-bounded-inbox/fixtures/inbox_64.json"


class InboxState:
    def __init__(self, items: list[dict[str, Any]]) -> None:
        self._lock = threading.RLock()
        self.inbox_revision = 1
        self.items = list(items)
        self._delay_ms = 0
        self._actions: dict[tuple[str, str], dict[str, Any]] = {}

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
            receipt_id = uuid.uuid4().hex
            started_ns = time.monotonic_ns()
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
                "page_receipt_id": receipt_id,
                "page_started_ns": started_ns,
                "page_completed_ns": time.monotonic_ns(),
            }

    def action(self, run_id: str, action_id: str, body: dict[str, Any]) -> dict[str, Any]:
        key = (run_id, action_id)
        with self._lock:
            cached = self._actions.get(key)
            if cached is not None:
                return dict(cached)
            if len(self._actions) >= 256:
                return {"ok": False, "error": "action_receipt_capacity",
                        "run_id": run_id, "action_id": action_id}
            name = str(body.get("name", ""))
            result: dict[str, Any]
            if name == "delay_ms":
                self._delay_ms = min(12000, max(0, int(body.get("ms", 500))))
                result = {"ok": True, "script": name}
            elif name == "push_item":
                item = body.get("item") or {}
                self.push_item(str(item.get("sender_name", "Lab")), str(item.get("preview", "Pushed item")))
                result = {"ok": True, "message_id": self.items[-1]["message_id"], "total": len(self.items)}
            elif name == "remove_message":
                message_id = str(body.get("viewed_message_id", ""))
                index = next((i for i, item in enumerate(self.items) if item.get("message_id") == message_id), -1)
                ok = self.remove_at(index)
                result = {"ok": ok, "removed_message_id": message_id if ok else None,
                          "total": len(self.items)}
                if ok:
                    selected = min(index, len(self.items) - 1)
                    result["nearest_remaining_message_id"] = self.items[selected]["message_id"] if selected >= 0 else None
            elif name == "reset":
                self.reset(SEED_ITEMS)
                result = {"ok": True, "total": len(self.items)}
            else:
                result = {"ok": False, "error": "unknown_script"}
            result.update({"run_id": run_id, "action_id": action_id,
                           "inbox_revision": self.inbox_revision,
                           "action_completed_ns": time.monotonic_ns()})
            self._actions[key] = dict(result)
            return result


def load_v1_fixture(path: Path) -> list[dict[str, Any]]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    items = raw.get("items") or []
    if not isinstance(items, list):
        raise ValueError("fixture items must be a list")
    return items


STATE: InboxState
SEED_ITEMS: list[dict[str, Any]]
OWNER_RUN_ID: str
FIXTURE_SHA256: str


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
            self._json_response(200, {"ok": True, "lab": "LAB-02", "schema": "fl.inbox-page/2", "run_id": OWNER_RUN_ID})
            return
        if parsed.path != "/v2/inbox":
            self._json_response(404, {"error": "not_found"})
            return
        qs = parse_qs(parsed.query)
        cursor = int((qs.get("cursor") or ["0"])[0])
        limit = int((qs.get("limit") or ["16"])[0])
        page = STATE.page(cursor, limit)
        page["run_id"] = OWNER_RUN_ID
        self._json_response(200, page)

    def do_POST(self) -> None:  # noqa: N802
        if self.path == "/lab/seed":
            length = int(self.headers.get("Content-Length", "0"))
            body = json.loads(self.rfile.read(length) or b"{}")
            if body.get("run_id") != OWNER_RUN_ID:
                self._json_response(409, {"ok": False, "error": "owner_mismatch"})
                return
            STATE.reset(SEED_ITEMS)
            self._json_response(200, {"ok": True, "run_id": OWNER_RUN_ID,
                                      "fixture_sha256": FIXTURE_SHA256,
                                      "count": len(SEED_ITEMS)})
            return
        parsed = urlparse(self.path)
        if parsed.path != "/lab/script":
            self._json_response(404, {"error": "not_found"})
            return
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length) or b"{}")
        run_id = str(body.get("run_id", ""))
        action_id = str(body.get("action_id", ""))
        if run_id != OWNER_RUN_ID or not action_id or len(action_id) > 128:
            self._json_response(409, {"ok": False, "error": "invalid_action_identity"})
            return
        result = STATE.action(run_id, action_id, body)
        self._json_response(200 if result.get("ok") else 400, result)


def main() -> int:
    global STATE, SEED_ITEMS, OWNER_RUN_ID, FIXTURE_SHA256
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--fixture", type=Path, default=DEFAULT_FIXTURE)
    ap.add_argument("--owner-run-id", default="standalone")
    args = ap.parse_args()
    fixture_path = args.fixture.resolve()
    fixture_bytes = fixture_path.read_bytes()
    FIXTURE_SHA256 = hashlib.sha256(fixture_bytes).hexdigest()
    OWNER_RUN_ID = args.owner_run_id
    SEED_ITEMS = load_v1_fixture(fixture_path)
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
