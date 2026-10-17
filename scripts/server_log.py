"""Bounded streaming log capture for owned fixture services."""
from __future__ import annotations

import re
import threading
from pathlib import Path


_URL = re.compile(rb"(?i)https?://[^\s\"'<>]+")
_ASSIGNMENT = re.compile(
    rb'''(?ix)(password|passwd|secret|token|authorization|ssid|credential|api[_-]?key)'''
    rb'''(\s*[=:]\s*|"\s*:\s*)'''
    rb'''("[^"\r\n]*"|'[^'\r\n]*'|[^\s,;}&]+)'''
)
_BEARER = re.compile(rb"(?i)\b(bearer|basic)\s+[A-Za-z0-9._~+/-]+=*")
_MARKER = b"[server log truncated]\n"
_LINE_MARKER = b"[server log line omitted: exceeds limit]\n"


def redact_line(value: bytes) -> bytes:
    """Redact common credentials and endpoints from one complete byte line."""
    value = _URL.sub(b"[REDACTED_URL]", value)
    value = _ASSIGNMENT.sub(lambda match: match.group(1) + match.group(2) + b"[REDACTED]", value)
    return _BEARER.sub(lambda match: match.group(1) + b" [REDACTED]", value)


class BoundedRedactingLog:
    """Write prefixed, sanitized service output to one byte-bounded run file.

    Data is redacted a line at a time, so credentials split across arbitrary
    pipe reads are still seen as a whole. An unterminated overlong line is
    discarded once it reaches the per-line cap, keeping memory bounded too.
    """

    def __init__(self, path: str | Path, *, max_bytes: int = 64 * 1024,
                 max_line_bytes: int = 8192):
        self.path = Path(path)
        if max_bytes < len(_MARKER) or max_line_bytes < 1:
            raise ValueError("invalid server log bounds")
        self.max_bytes = max_bytes
        self.max_line_bytes = max_line_bytes
        self._written = 0
        self._truncated = False
        self._lock = threading.Lock()
        self._buffers = {"stdout": bytearray(), "stderr": bytearray()}
        self._discarding = {"stdout": False, "stderr": False}
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if self.path.exists() or self.path.is_symlink():
            raise FileExistsError(f"server log already exists: {self.path}")
        self._handle = self.path.open("xb")

    @property
    def truncated(self) -> bool:
        return self._truncated

    def _write(self, data: bytes) -> None:
        if self._truncated:
            return
        remaining = self.max_bytes - self._written
        if len(data) <= remaining:
            self._handle.write(data)
            self._written += len(data)
            return
        marker = _MARKER[:remaining]
        self._handle.write(marker)
        self._written += len(marker)
        self._truncated = True

    def feed(self, stream: str, chunk: bytes) -> None:
        if stream not in self._buffers:
            raise ValueError("unknown server log stream")
        with self._lock:
            if self._handle.closed or self._truncated:
                return
            pending = self._buffers[stream]
            for byte in chunk:
                if self._discarding[stream]:
                    if byte == 10:
                        self._write(_LINE_MARKER)
                        self._discarding[stream] = False
                    continue
                pending.append(byte)
                if byte == 10:
                    prefix = f"[{stream}] ".encode("ascii")
                    self._write(prefix + redact_line(bytes(pending)))
                    pending.clear()
                elif len(pending) > self.max_line_bytes:
                    pending.clear()
                    self._discarding[stream] = True

    def close(self) -> None:
        with self._lock:
            if self._handle.closed:
                return
            for stream, pending in self._buffers.items():
                if self._discarding[stream]:
                    self._write(_LINE_MARKER)
                    self._discarding[stream] = False
                elif pending:
                    prefix = f"[{stream}] ".encode("ascii")
                    self._write(prefix + redact_line(bytes(pending)) + b"\n")
                    pending.clear()
            self._handle.flush()
            self._handle.close()
