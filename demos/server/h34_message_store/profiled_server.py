#!/usr/bin/env python3
"""Isolated ASGI launcher for request allocation measurements.

The ASGI wrapper covers response streaming as well as endpoint execution. Only
one measured request is sent at a time by qualify.py.
"""

from __future__ import annotations

import argparse
import json
import os
import resource
import sys
import tracemalloc
from pathlib import Path

import uvicorn

from demos.server.h34_message_store.server import app


def rss_peak_bytes() -> int:
    value = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return int(value if sys.platform == "darwin" else value * 1024)


class MeasuredApp:
    def __init__(self, inner, output: Path):
        self.inner = inner
        self.output = output

    async def __call__(self, scope, receive, send):
        if scope["type"] != "http":
            await self.inner(scope, receive, send)
            return
        route = scope["method"] + " " + scope["path"]
        # Current live allocations are the request baseline; reset_peak keeps
        # earlier requests from contaminating this request's reported peak.
        before, _ = tracemalloc.get_traced_memory()
        tracemalloc.reset_peak()
        status = None
        max_response_chunk = 0
        max_request_chunk = 0

        async def measured_receive():
            nonlocal max_request_chunk
            message = await receive()
            if message["type"] == "http.request":
                max_request_chunk = max(max_request_chunk, len(message.get("body", b"")))
            return message

        async def measured_send(message):
            nonlocal status, max_response_chunk
            if message["type"] == "http.response.start":
                status = message["status"]
            elif message["type"] == "http.response.body":
                max_response_chunk = max(max_response_chunk, len(message.get("body", b"")))
            await send(message)

        try:
            await self.inner(scope, measured_receive, measured_send)
        finally:
            _, peak = tracemalloc.get_traced_memory()
            record = {
                "route": route,
                "status": status,
                "python_peak_over_baseline_bytes": max(0, peak - before),
                "python_baseline_bytes": before,
                "rss_peak_bytes": rss_peak_bytes(),
                "max_request_chunk_bytes": max_request_chunk,
                "max_response_chunk_bytes": max_response_chunk,
            }
            with self.output.open("a", encoding="utf-8") as fh:
                fh.write(json.dumps(record, sort_keys=True) + "\n")
                fh.flush()
                os.fsync(fh.fileno())


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--metrics", type=Path, required=True)
    args = parser.parse_args()
    tracemalloc.start(8)
    uvicorn.run(MeasuredApp(app, args.metrics), host="127.0.0.1", port=args.port, log_level="warning")


if __name__ == "__main__":
    main()
