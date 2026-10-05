#!/usr/bin/env python3
"""Run one server demo: start its server, run its client, stop the server.

  python scripts/run_server_demo.py 01_auth
"""

from __future__ import annotations

import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEMOS = ROOT / "demos" / "server"


def _venv_python() -> str:
    if os.name == "nt":
        cand = ROOT / ".venv" / "Scripts" / "python.exe"
    else:
        cand = ROOT / ".venv" / "bin" / "python"
    if cand.is_file():
        return str(cand)
    return sys.executable


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def _wait_port(port: int, timeout: float = 8.0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.2)
            try:
                sock.connect(("127.0.0.1", port))
                return True
            except OSError:
                time.sleep(0.05)
    return False


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run one standalone server demo.")
    parser.add_argument("name", help="Directory under demos/server/ (e.g. 01_auth)")
    args = parser.parse_args(argv)
    folder = DEMOS / args.name
    server = folder / "server.py"
    client = folder / "client.py"
    if not server.is_file() or not client.is_file():
        print(f"missing {server} or {client}", file=sys.stderr)
        return 2

    py = _venv_python()
    port = _free_port()
    env = os.environ.copy()
    env["PYTHONPATH"] = str(ROOT) + os.pathsep + env.get("PYTHONPATH", "")
    env["FAMILY_LINK_ROOT"] = str(ROOT)
    isolated = tempfile.TemporaryDirectory(prefix="family-link-h34-") if args.name == "h34_message_store" else None
    if isolated is not None:
        env["FAMILY_LINK_ROOT"] = isolated.name
        registry = Path(isolated.name) / "devices.yaml"
        registry.write_text(
            "devices:\n"
            "  - {id: box-a, token: demo-sender, role: child, peer: box-b}\n"
            "  - {id: box-b, token: demo-recipient, role: child, peer: box-a}\n"
            "  - {id: box-c, token: demo-unrelated, role: child, peer: ''}\n"
            "  - {id: box-admin, token: demo-admin, role: admin, peer: ''}\n",
            encoding="utf-8",
        )
        env["FAMILY_LINK_DEVICES_REGISTRY"] = str(registry)
    proc = subprocess.Popen(
        [py, str(server), "--host", "127.0.0.1", "--port", str(port)],
        cwd=str(ROOT),
        env=env,
    )
    try:
        if not _wait_port(port):
            print(f"server {args.name} did not bind :{port}", file=sys.stderr)
            return 1
        client_run = subprocess.run(
            [
                py,
                str(client),
                "--base-url",
                f"http://127.0.0.1:{port}",
            ],
            cwd=str(ROOT),
            env=env,
        )
        return client_run.returncode
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        if isolated is not None:
            isolated.cleanup()


if __name__ == "__main__":
    sys.exit(main())
