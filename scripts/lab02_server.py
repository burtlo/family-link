"""Owned LAB-02 fixture server lifecycle for host workflows."""
from __future__ import annotations

import hashlib
import errno
import json
import select
import socket
import subprocess
import sys
import time
import threading
import queue
import urllib.error
from urllib.parse import urlsplit
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from lab02_workflow_config import Lab02WorkflowConfig

ROOT = Path(__file__).resolve().parent.parent
FIXTURE_DIR = ROOT / "client/labs/lab-02-bounded-inbox/fixtures"
FIXTURE_CATALOG = {size: FIXTURE_DIR / f"inbox_{size}.json" for size in (8, 16, 64, 256)}


class ServerLifecycleError(RuntimeError):
    """A managed server stage failed; details contain no URL or credentials."""


@dataclass(frozen=True)
class FixtureIdentity:
    size: int
    path: Path
    sha256: str


def select_fixture(size: int) -> FixtureIdentity:
    path = FIXTURE_CATALOG.get(size)
    if path is None or not path.is_file():
        raise ServerLifecycleError("unsupported_fixture")
    try:
        raw = path.read_bytes()
        parsed = json.loads(raw)
        items = parsed["items"]
        if not isinstance(items, list) or len(items) != size:
            raise ValueError
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError):
        raise ServerLifecycleError("invalid_fixture") from None
    return FixtureIdentity(size=size, path=path, sha256=hashlib.sha256(raw).hexdigest())


class ManagedLab02Server:
    """Own one child process and never operate on a listener it did not start."""

    def __init__(self, config: Lab02WorkflowConfig, *, run_id: str | None = None,
                 event_sink: Callable[[dict], None] | None = None):
        self.config = config
        self.run_id = run_id or uuid.uuid4().hex
        self._events = event_sink or (lambda event: None)
        self._terminal_actions: set[str] = set()
        self._action_lock = threading.Lock()
        self._seen_lock = threading.Lock()
        self._seen_actions: set[str] = set()
        self._spawn_thread: threading.Thread | None = None
        self._spawn_result: queue.Queue = queue.Queue(maxsize=1)
        self._spawn_cancel = threading.Event()
        self._process: subprocess.Popen | None = None
        self.fixture: FixtureIdentity | None = None
        self.base_url = config.client_url.rstrip("/")
        parsed = urlsplit(config.client_url)
        self._client_host = parsed.hostname or "127.0.0.1"

    def _event(self, action: str, stage: str, outcome: str, reason: str = "") -> None:
        self._events({"run_id": self.run_id, "action_id": action, "stage": stage,
                      "outcome": outcome, "reason": reason,
                      "monotonic_ns": time.monotonic_ns()})

    def _terminal_event(self, action: str, stage: str, outcome: str, reason: str = "") -> None:
        if action in self._terminal_actions:
            return
        self._terminal_actions.add(action)
        self._event(action, stage, outcome, reason)

    def _deadline(self, key: str) -> float:
        return time.monotonic() + self.config.deadlines_seconds[key]

    def _request(self, path: str, *, payload: dict | None = None, timeout: float = 1.0,
                 max_bytes: int = 65536, cancelled: threading.Event | None = None) -> dict:
        deadline = time.monotonic() + timeout
        data = None if payload is None else json.dumps(payload).encode("utf-8")
        body = b"" if data is None else data
        headers = [f"Host: {self._client_host}:{self.config.bind_port}",
                   "Connection: close", "Accept: application/json"]
        if data is not None:
            headers.extend(("Content-Type: application/json", f"Content-Length: {len(body)}"))
        request = (f"{'POST' if data is not None else 'GET'} {path} HTTP/1.1\r\n"
                   + "\r\n".join(headers) + "\r\n\r\n").encode("ascii") + body
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setblocking(False)
        try:
            err = sock.connect_ex((self._client_host, self.config.bind_port))
            if err not in (0, errno.EINPROGRESS, errno.EWOULDBLOCK, errno.EALREADY, 10035):
                raise OSError(err, "connect failed")
            def wait(readable: bool) -> None:
                if cancelled is not None and cancelled.is_set():
                    raise InterruptedError("request_cancelled")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("request_deadline")
                ready = select.select([sock] if readable else [], [] if readable else [sock], [], remaining)
                if not ready[0] and not ready[1]:
                    raise TimeoutError("request_deadline")
            if err:
                wait(False)
                connect_error = sock.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
                if connect_error:
                    raise OSError(connect_error, "connect failed")
            view = memoryview(request)
            sent = 0
            while sent < len(view):
                wait(False)
                count = sock.send(view[sent:])
                if count == 0:
                    raise OSError("socket closed while sending")
                sent += count
            response = bytearray()
            marker = -1
            while marker < 0:
                if len(response) > 8192:
                    raise ServerLifecycleError("response_headers_too_large")
                wait(True)
                chunk = sock.recv(4096)
                if not chunk:
                    raise ServerLifecycleError("invalid_response")
                response.extend(chunk)
                marker = response.find(b"\r\n\r\n")
            head = bytes(response[:marker]).decode("iso-8859-1")
            fields = head.split("\r\n")
            status = int(fields[0].split(" ", 2)[1])
            response_headers = {}
            for field in fields[1:]:
                name, value = field.split(":", 1)
                response_headers[name.lower().strip()] = value.strip()
            raw_length = response_headers.get("content-length")
            length = int(raw_length) if raw_length is not None else -1
            if length > max_bytes:
                raise ServerLifecycleError("response_too_large")
            result = bytearray(response[marker + 4:])
            while length < 0 or len(result) < length:
                wait(True)
                chunk = sock.recv(min(4096, max_bytes + 1 - len(result)) if length < 0
                                  else min(4096, length - len(result)))
                if not chunk:
                    if length < 0:
                        break
                    raise ServerLifecycleError("invalid_response")
                result.extend(chunk)
                if len(result) > max_bytes:
                    raise ServerLifecycleError("response_too_large")
            if time.monotonic() > deadline:
                raise TimeoutError("request_deadline")
            # Preserve HTTP status in the returned mapping for truthful caller checks.
            decoded = json.loads(result[:length].decode("utf-8"))
            if not isinstance(decoded, dict):
                raise ServerLifecycleError("invalid_response")
            decoded.setdefault("_http_status", status)
            return decoded
        finally:
            sock.close()

    def _spawn_owned(self, argv: list[str], deadline: float) -> subprocess.Popen:
        """Run process creation in a tracked worker; a late child is reaped by that worker."""
        self._spawn_cancel.clear()
        self._spawn_result = queue.Queue(maxsize=1)
        def launch() -> None:
            try:
                child = subprocess.Popen(argv, cwd=str(ROOT), stdout=subprocess.DEVNULL,
                                         stderr=subprocess.DEVNULL)
                if self._spawn_cancel.is_set():
                    child.terminate()
                    try:
                        child.wait(timeout=self.config.deadlines_seconds["cleanup"])
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait(timeout=self.config.deadlines_seconds["cleanup"])
                    self._spawn_result.put((None, "cancelled"))
                else:
                    self._spawn_result.put((child, None))
            except BaseException as exc:
                self._spawn_result.put((None, exc))
        self._spawn_thread = threading.Thread(target=launch, name=f"lab02-spawn-{self.run_id}", daemon=True)
        self._spawn_thread.start()
        try:
            child, error = self._spawn_result.get(timeout=max(0, deadline-time.monotonic()))
        except queue.Empty:
            self._spawn_cancel.set()
            raise ServerLifecycleError("startup_timeout") from None
        if error:
            raise ServerLifecycleError("server_spawn_failed") from None
        if child is None:
            raise ServerLifecycleError("startup_timeout")
        self._spawn_thread.join(timeout=max(0, deadline-time.monotonic()))
        if self._spawn_thread.is_alive():
            self._spawn_cancel.set()
            # Keep ownership of the worker so close() can retry/join it.
            self._process = child
            raise ServerLifecycleError("startup_timeout")
        self._spawn_thread = None
        return child

    def _listener_occupied(self) -> bool:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.settimeout(0.25)
            return probe.connect_ex((self._client_host, self.config.bind_port)) == 0

    def start(self, fixture_size: int | None = None) -> FixtureIdentity:
        if self._process is not None:
            raise ServerLifecycleError("already_started")
        size = fixture_size or self.config.fixture_size_default
        try:
            self.fixture = select_fixture(size)
            if self._listener_occupied():
                self._terminal_event("server_bind", "SERVER_START", "failure", "port_occupied")
                raise ServerLifecycleError("port_occupied")
            self._event("server_bind", "SERVER_START", "success")
            argv = [sys.executable, str(self.config.server_path), "--host", self.config.bind_host,
                    "--port", str(self.config.bind_port), "--fixture", str(self.fixture.path),
                    "--owner-run-id", self.run_id]
            self._process = self._spawn_owned(argv, self._deadline("startup"))
            self._terminal_event("server_start", "SERVER_START", "success")
            self._wait_health()
            self._seed()
            return self.fixture
        except BaseException as exc:
            reason = exc.args[0] if isinstance(exc, ServerLifecycleError) and exc.args else "server_start_failed"
            self._terminal_event("server_start", "SERVER_START", "failure", str(reason))
            self.close()
            if isinstance(exc, ServerLifecycleError):
                raise
            if not isinstance(exc, Exception):
                raise
            raise ServerLifecycleError("server_start_failed") from None

    def _wait_health(self) -> None:
        deadline = self._deadline("health")
        while time.monotonic() < deadline:
            if self._process is None or self._process.poll() is not None:
                self._terminal_event("server_health", "SERVER_HEALTH", "failure", "child_exited")
                raise ServerLifecycleError("child_exited")
            try:
                body = self._request("/lab/health", timeout=min(0.5, max(0.05, deadline-time.monotonic())))
                if body.get("_http_status") == 200 and body.get("ok") and body.get("run_id") == self.run_id:
                    self._terminal_event("server_health", "SERVER_HEALTH", "success")
                    return
            except (OSError, urllib.error.URLError, ValueError, json.JSONDecodeError):
                pass
            time.sleep(min(0.05, max(0, deadline - time.monotonic())))
        self._terminal_event("server_health", "SERVER_HEALTH", "failure", "health_timeout")
        raise ServerLifecycleError("health_timeout")

    def _seed(self) -> None:
        assert self.fixture is not None
        deadline = self._deadline("seed")
        try:
            receipt = self._request("/lab/seed", payload={"run_id": self.run_id},
                                    timeout=min(2.0, max(0.05, deadline-time.monotonic())))
            if (receipt.get("_http_status") != 200 or receipt.get("ok") is not True
                    or self._process is None or self._process.poll() is not None
                    or receipt.get("run_id") != self.run_id
                    or receipt.get("fixture_sha256") != self.fixture.sha256
                    or receipt.get("count") != self.fixture.size):
                raise ServerLifecycleError("seed_receipt_mismatch")
            self._terminal_event("server_seed", "SERVER_SEED", "success")
        except BaseException as exc:
            reason = "seed_receipt_mismatch" if isinstance(exc, ServerLifecycleError) else (
                "interrupted" if not isinstance(exc, Exception) else "seed_failed")
            self._terminal_event("server_seed", "SERVER_SEED",
                                 "cancelled" if not isinstance(exc, Exception) else "failure", reason)
            if not isinstance(exc, Exception):
                raise
            if isinstance(exc, ServerLifecycleError):
                raise
            raise ServerLifecycleError("seed_failed") from None

    def close(self) -> None:
        process = self._process
        worker = self._spawn_thread
        if worker is not None:
            self._spawn_cancel.set()
            worker.join(timeout=self.config.deadlines_seconds["cleanup"])
            if worker.is_alive():
                self._terminal_event("server_cleanup", "CLEANUP", "failure", "spawn_cleanup_timeout")
                raise ServerLifecycleError("cleanup_failed")
            self._spawn_thread = None
            if process is None:
                try:
                    late_child, _ = self._spawn_result.get_nowait()
                    process = late_child
                    self._process = process
                except queue.Empty:
                    pass
        if process is None:
            return
        try:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=self.config.deadlines_seconds["cleanup"])
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=self.config.deadlines_seconds["cleanup"])
            self._process = None
            self._terminal_event("server_cleanup", "CLEANUP", "success")
        except Exception:
            self._terminal_event("server_cleanup", "CLEANUP", "failure", "cleanup_failed")
            raise ServerLifecycleError("cleanup_failed") from None

    def post_action(self, action_id: str, payload: dict, *, cancelled: threading.Event | None = None) -> dict:
        """Perform one serialized script request with one bounded terminal event."""
        with self._seen_lock:
            if action_id in self._seen_actions:
                raise ServerLifecycleError("duplicate_action_id")
            self._seen_actions.add(action_id)
        frozen_payload = json.loads(json.dumps(payload))
        frozen_payload["run_id"] = self.run_id
        frozen_payload["action_id"] = action_id
        deadline = self._deadline("action")
        acquired = False
        try:
            while time.monotonic() < deadline:
                if cancelled is not None and cancelled.is_set():
                    raise InterruptedError
                acquired = self._action_lock.acquire(timeout=min(0.05, deadline-time.monotonic()))
                if acquired:
                    break
            if not acquired:
                raise TimeoutError("action_lock_timeout")
        except BaseException as exc:
            self._terminal_event(action_id, "SCENARIO", "cancelled" if not isinstance(exc, Exception) or isinstance(exc, InterruptedError) else "failure",
                                 "cancelled" if isinstance(exc, InterruptedError) else "interrupted" if not isinstance(exc, Exception) else "action_timeout")
            if acquired:
                self._action_lock.release()
            if isinstance(exc, KeyboardInterrupt):
                raise
            raise ServerLifecycleError("action_cancelled" if isinstance(exc, InterruptedError) else "action_failed") from None
        try:
            if cancelled is not None and cancelled.is_set():
                raise InterruptedError
            result = self._request("/lab/script", payload=frozen_payload,
                                   timeout=max(0.001, deadline-time.monotonic()), cancelled=cancelled)
            if (result.get("_http_status") != 200 or result.get("ok") is not True
                    or result.get("run_id") != self.run_id
                    or result.get("action_id") != action_id):
                raise ServerLifecycleError("action_rejected")
            self._terminal_event(action_id, "SCENARIO", "success")
            return result
        except BaseException as exc:
            interrupted = isinstance(exc, (KeyboardInterrupt, InterruptedError))
            self._terminal_event(action_id, "SCENARIO", "cancelled" if interrupted else "failure",
                                 "interrupted" if interrupted else "action_rejected" if isinstance(exc, ServerLifecycleError) else "action_timeout_or_transport")
            if isinstance(exc, KeyboardInterrupt):
                raise
            raise ServerLifecycleError("action_cancelled" if interrupted else "action_failed") from None
        finally:
            self._action_lock.release()

    def __enter__(self) -> "ManagedLab02Server":
        self.start()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()
