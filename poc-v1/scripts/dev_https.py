#!/usr/bin/env python3
"""Local TLS certs + HTTPS demo server for family-link.

Prefers mkcert (trusted on this Mac after `mkcert -install`). Without mkcert,
prints the brew hint. `--insecure-self-signed` makes a throwaway openssl cert
for ESP32 skip-verify LAN demos (Safari on iPhone will warn).

Certs go in gitignored data/certs/. Never commit PEM files.

  python scripts/dev_https.py
  python scripts/dev_https.py --extra-name 192.168.8.143
  python scripts/dev_https.py --insecure-self-signed
  python scripts/dev_https.py --certs-only

Then flash firmware h16 with DEMO_SERVER_HOST=<LAN IP> and DEMO_SERVER_PORT=8443.
See docs/TLS.md.
"""

from __future__ import annotations

import argparse
import ipaddress
import os
import shutil
import socket
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CERT_DIR = ROOT / "data" / "certs"
CERT_FILE = CERT_DIR / "dev.pem"
KEY_FILE = CERT_DIR / "dev-key.pem"
COMBINED = ROOT / "demos" / "server" / "combined" / "server.py"
MKCERT_HINT = "brew install mkcert nss; mkcert -install"


def _venv_python() -> str:
    if os.name == "nt":
        cand = ROOT / ".venv" / "Scripts" / "python.exe"
    else:
        cand = ROOT / ".venv" / "bin" / "python"
    if cand.is_file():
        return str(cand)
    return sys.executable


def _guess_lan_ip() -> str | None:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(("8.8.8.8", 80))
            ip = sock.getsockname()[0]
        if ip and not ip.startswith("127."):
            return ip
    except OSError:
        return None
    return None


def _san_entry(name: str) -> str:
    try:
        addr = ipaddress.ip_address(name)
    except ValueError:
        return f"DNS:{name}"
    if isinstance(addr, ipaddress.IPv6Address):
        return f"IP:{addr.exploded}"
    return f"IP:{addr}"


def _run(cmd: list[str]) -> None:
    print("-> " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, cwd=str(ROOT))


def _ensure_cert_dir() -> None:
    CERT_DIR.mkdir(parents=True, exist_ok=True)


def generate_mkcert(names: list[str]) -> None:
    _ensure_cert_dir()
    cmd = [
        "mkcert",
        "-cert-file",
        str(CERT_FILE),
        "-key-file",
        str(KEY_FILE),
        *names,
    ]
    _run(cmd)
    print(f"mkcert wrote {CERT_FILE.relative_to(ROOT)} (install CA with: mkcert -install)")


def generate_self_signed(names: list[str]) -> None:
    openssl = shutil.which("openssl")
    if not openssl:
        raise SystemExit(
            "openssl not found. Install it, or install mkcert:\n" f"  {MKCERT_HINT}"
        )
    _ensure_cert_dir()
    san = ",".join(_san_entry(n) for n in names)
    cmd = [
        openssl,
        "req",
        "-x509",
        "-newkey",
        "rsa:2048",
        "-sha256",
        "-days",
        "14",
        "-nodes",
        "-keyout",
        str(KEY_FILE),
        "-out",
        str(CERT_FILE),
        "-subj",
        "/CN=localhost",
        "-addext",
        f"subjectAltName={san}",
    ]
    _run(cmd)
    print(
        f"self-signed wrote {CERT_FILE.relative_to(ROOT)} "
        "(not trusted on iPhone; h16 skip-verify is OK)"
    )


def _print_run_help(host: str, port: int) -> None:
    rel_cert = CERT_FILE.relative_to(ROOT)
    rel_key = KEY_FILE.relative_to(ROOT)
    ssl_flags = (
        f"--host {host} --port {port} "
        f"--ssl-certfile {rel_cert} --ssl-keyfile {rel_key}"
    )
    print()
    print("Run with TLS (script starts combined when present, else 01_auth):")
    print(f"  python -m demos.server.combined.server {ssl_flags}")
    print(f"  python -m demos.server.01_auth.server {ssl_flags}")
    lan = _guess_lan_ip()
    if lan:
        print()
        print(f"This Mac's LAN IP looks like {lan}. secrets.h:")
        print(f'  #define DEMO_SERVER_HOST "{lan}"')
        print("  #define DEMO_SERVER_PORT 8443")
        print("  (h07 stays http on 8080; h16 always uses https)")


def run_server(host: str, port: int) -> None:
    if COMBINED.is_file():
        module = "demos.server.combined.server"
        print(f"HTTPS combined → https://{host}:{port}")
    else:
        module = "demos.server.01_auth.server"
        print(f"HTTPS 01_auth → https://{host}:{port}  (GET /v1/me)")
    cmd = [
        _venv_python(),
        "-m",
        module,
        "--host",
        host,
        "--port",
        str(port),
        "--ssl-certfile",
        str(CERT_FILE),
        "--ssl-keyfile",
        str(KEY_FILE),
    ]
    env = os.environ.copy()
    env["PYTHONPATH"] = str(ROOT) + os.pathsep + env.get("PYTHONPATH", "")
    print("-> " + " ".join(cmd), flush=True)
    raise SystemExit(subprocess.run(cmd, cwd=str(ROOT), env=env).returncode)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Generate local TLS certs and run an HTTPS demo server."
    )
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8443)
    parser.add_argument(
        "--extra-name",
        action="append",
        default=[],
        help="SAN name (LAN IP or hostname). Repeatable. iPhone needs this; h16 skip-verify does not.",
    )
    parser.add_argument(
        "--insecure-self-signed",
        action="store_true",
        help="If mkcert is missing, make a throwaway openssl cert (LAN skip-verify only).",
    )
    parser.add_argument(
        "--certs-only",
        action="store_true",
        help="Write certs and print the uvicorn command; do not bind a port.",
    )
    args = parser.parse_args(argv)

    names = ["localhost", "127.0.0.1", "::1", *args.extra_name]
    # Unique, stable order.
    seen: set[str] = set()
    uniq: list[str] = []
    for name in names:
        if name not in seen:
            seen.add(name)
            uniq.append(name)

    mkcert = shutil.which("mkcert")
    if mkcert:
        generate_mkcert(uniq)
    else:
        print(f"mkcert not found. Install and trust a local CA:\n  {MKCERT_HINT}")
        if not args.insecure_self_signed:
            print("Or re-run with --insecure-self-signed (openssl throwaway; ESP32 skip-verify).")
            return 2
        generate_self_signed(uniq)

    _print_run_help(args.host, args.port)
    if args.certs_only:
        return 0
    run_server(args.host, args.port)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except subprocess.CalledProcessError as exc:
        print(f"  [fail] command exited {exc.returncode}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(130)
