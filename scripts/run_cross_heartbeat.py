#!/usr/bin/env python3
"""Cross-Wi-Fi heartbeat demo: server on this Mac, BOX-3 on 2.4 GHz.

Starts 02_heartbeat on 0.0.0.0, optionally a public HTTPS tunnel, flashes
firmware/demos/h17_cross_heartbeat.c with a reachable host, and heartbeats
as box-b so the box sees peer_online.

The box joins 2.4 GHz (SSID in firmware/secrets.h). This Mac can sit on
another SSID (5 GHz is fine). Same router without client isolation is
enough. Isolated networks: --tunnel (cloudflared) or --host.

  make demo-cross-heartbeat
  make demo-cross-heartbeat TUNNEL=1
  make demo-cross-heartbeat SERVER_HOST=100.x.y.z
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import IO

ROOT = Path(__file__).resolve().parent.parent
FIRMWARE = ROOT / "firmware"
SERVER = ROOT / "demos" / "server" / "02_heartbeat" / "server.py"
FLASH = ROOT / "scripts" / "flash.py"
CF_URL_RE = re.compile(r"https://[a-z0-9-]+\.trycloudflare\.com", re.I)
PLACEHOLDER_SSIDS = {"your-2.4ghz-ssid"}


def venv_python() -> str:
    if os.name == "nt":
        cand = ROOT / ".venv" / "Scripts" / "python.exe"
    else:
        cand = ROOT / ".venv" / "bin" / "python"
    if cand.is_file():
        return str(cand)
    return sys.executable


def lan_ipv4s() -> list[str]:
    found: list[str] = []
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(("8.8.8.8", 80))
            ip = sock.getsockname()[0]
            if not ip.startswith("127."):
                found.append(ip)
    except OSError:
        pass
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = info[4][0]
            if ip not in found and not ip.startswith("127."):
                found.append(ip)
    except OSError:
        pass
    return found


def parse_cloudflared_url(text: str) -> str | None:
    match = CF_URL_RE.search(text)
    return match.group(0) if match else None


def read_c_string(path: Path, name: str) -> str | None:
    if not path.is_file():
        return None
    needle = f"#define {name}"
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line.startswith(needle):
            continue
        rest = line[len(needle) :].strip()
        if rest.startswith('"') and rest.endswith('"') and len(rest) >= 2:
            return rest[1:-1]
        return rest.split()[0] if rest else None
    return None


def secrets_ssid() -> str | None:
    return read_c_string(FIRMWARE / "secrets.h", "DEMO_WIFI_SSID") or read_c_string(
        FIRMWARE / "secrets.example.h", "DEMO_WIFI_SSID"
    )


def load_tokens() -> tuple[str, str]:
    sys.path.insert(0, str(ROOT))
    from demos.server._shared.registry import load_devices

    devices = load_devices()
    box_a = devices.get("box-a")
    box_b = devices.get("box-b")
    if box_a is None or box_b is None:
        raise SystemExit("devices.example.yaml must define box-a and box-b")
    return box_a.token, box_b.token


def wait_port(host: str, port: int, timeout: float = 12.0) -> bool:
    deadline = time.time() + timeout
    probe = "127.0.0.1" if host in {"0.0.0.0", "::"} else host
    while time.time() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.3)
            try:
                sock.connect((probe, port))
                return True
            except OSError:
                time.sleep(0.05)
    return False


def pipe_prefix(prefix: str, stream: IO[str] | None, extra: list[str] | None = None) -> None:
    if stream is None:
        return
    for line in stream:
        text = line.rstrip()
        print(f"{prefix} {text}", flush=True)
        if extra is not None:
            extra.append(text)


def start_server(py: str, bind: str, port: int) -> subprocess.Popen[str]:
    env = os.environ.copy()
    env["PYTHONPATH"] = str(ROOT) + os.pathsep + env.get("PYTHONPATH", "")
    env["PYTHONUNBUFFERED"] = "1"
    proc = subprocess.Popen(
        [py, str(SERVER), "--host", bind, "--port", str(port)],
        cwd=str(ROOT),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    threading.Thread(target=pipe_prefix, args=("[server]", proc.stdout), daemon=True).start()
    if not wait_port(bind, port):
        proc.terminate()
        raise SystemExit(f"heartbeat server did not bind {bind}:{port}")
    return proc


def start_cloudflared(port: int) -> tuple[subprocess.Popen[str], str]:
    bin_path = shutil.which("cloudflared")
    if not bin_path:
        raise SystemExit(
            "cloudflared not found. brew install cloudflared\n"
            "  or pick a host the box can already reach:\n"
            "  make demo-cross-heartbeat SERVER_HOST=100.x.y.z"
        )
    lines: list[str] = []
    proc = subprocess.Popen(
        [
            bin_path,
            "tunnel",
            "--url",
            f"http://127.0.0.1:{port}",
            "--no-autoupdate",
        ],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    threading.Thread(
        target=pipe_prefix, args=("[tunnel]", proc.stdout, lines), daemon=True
    ).start()
    deadline = time.time() + 40
    while time.time() < deadline:
        url = parse_cloudflared_url("\n".join(lines))
        if url:
            host = url.removeprefix("https://").removeprefix("http://").split("/")[0]
            return proc, host
        if proc.poll() is not None:
            raise SystemExit("cloudflared exited before publishing a URL")
        time.sleep(0.2)
    raise SystemExit("cloudflared did not print a trycloudflare.com URL in 40s")


def peer_loop(base: str, token: str, stop: threading.Event) -> None:
    try:
        import httpx
    except ImportError:
        print("[peer] httpx missing. make install-server", flush=True)
        return
    headers = {"Authorization": f"Bearer {token}"}
    with httpx.Client(timeout=8.0, verify=False) as client:
        while not stop.is_set():
            try:
                r = client.post(f"{base}/v1/heartbeat", headers=headers, json={"uptime_s": 0})
                if r.status_code != 200:
                    print(f"[peer] heartbeat {r.status_code} {r.text[:80]}", flush=True)
            except Exception as exc:  # noqa: BLE001 — demo loop; keep beating
                print(f"[peer] {exc}", flush=True)
            stop.wait(4.0)


def poll_status(base: str, stop: threading.Event) -> None:
    try:
        import httpx
    except ImportError:
        return
    last: dict[str, str] = {}
    with httpx.Client(timeout=8.0, verify=False) as client:
        while not stop.is_set():
            try:
                r = client.get(f"{base}/v1/status")
                if r.status_code == 200:
                    for row in r.json().get("devices") or []:
                        key = f"{row.get('id')} {row.get('online')} {row.get('client')}"
                        ident = str(row.get("id"))
                        if last.get(ident) != key:
                            last[ident] = key
                            flag = "online " if row.get("online") else "offline"
                            print(
                                f"[status] {ident} {flag} client={row.get('client') or '—'} "
                                f"age_s={row.get('age_s')}",
                                flush=True,
                            )
            except Exception:
                pass
            stop.wait(2.0)


def flash_h17(host: str, port: int, tls: int, serial: str | None) -> int:
    cmd = [
        venv_python(),
        str(FLASH),
        "--demo",
        "h17",
        "--server-host",
        host,
        "--server-port",
        str(port),
        "--server-tls",
        str(tls),
    ]
    if serial:
        cmd.extend(["--port", serial])
    print("-> " + " ".join(cmd), flush=True)
    return subprocess.run(cmd, cwd=str(ROOT)).returncode


def banner(
    ssid: str | None,
    bind: str,
    bind_port: int,
    box_host: str,
    box_port: int,
    tls: bool,
    ips: list[str],
) -> None:
    scheme = "https" if tls else "http"
    print("", flush=True)
    print("Cross-Wi-Fi heartbeat", flush=True)
    print(f"  Box SSID (2.4 GHz, from secrets.h): {ssid or '(missing secrets.h)'}", flush=True)
    print(f"  Server bind:   {bind}:{bind_port}", flush=True)
    print(f"  Box will call: {scheme}://{box_host}:{box_port}/v1/heartbeat", flush=True)
    if ips:
        print(f"  This Mac IPv4: {', '.join(ips)}", flush=True)
    print(
        "  Put this Mac on a *different* SSID than the box if you can "
        "(5 GHz is fine). Same router, no client isolation.",
        flush=True,
    )
    print("  Isolated networks: TUNNEL=1 or SERVER_HOST=<reachable name>", flush=True)
    print("  Ctrl-C stops the server. Box keeps beating until you reflash.", flush=True)
    print("", flush=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0", help="Address the heartbeat server listens on")
    parser.add_argument("--bind-port", type=int, default=8080, help="Local listen port")
    parser.add_argument("--host", help="Host baked into firmware (default: this Mac's LAN IPv4)")
    parser.add_argument("--port", type=int, help="Port baked into firmware (default: bind-port)")
    parser.add_argument("--tls", action="store_true", help="Box uses HTTPS skip-verify")
    parser.add_argument(
        "--tunnel",
        action="store_true",
        help="Publish HTTPS via cloudflared (trycloudflare.com) and flash that host",
    )
    parser.add_argument("--skip-flash", action="store_true", help="Run the server only")
    parser.add_argument("--no-peer", action="store_true", help="Do not heartbeat as box-b")
    parser.add_argument("--serial", help="USB serial for flash (or PORT= / ESPPORT=)")
    args = parser.parse_args(argv)

    ssid = secrets_ssid()
    if ssid is None or ssid in PLACEHOLDER_SSIDS:
        print(
            "Edit firmware/secrets.h and set DEMO_WIFI_SSID / DEMO_WIFI_PASS "
            "to the 2.4 GHz network the BOX-3 should join.",
            file=sys.stderr,
        )
        if ssid in PLACEHOLDER_SSIDS:
            return 2

    py = venv_python()
    try:
        _token_a, token_b = load_tokens()
    except Exception as exc:  # noqa: BLE001
        print(f"device registry: {exc}", file=sys.stderr)
        print("make install-server", file=sys.stderr)
        return 2

    children: list[subprocess.Popen[str]] = []
    stop = threading.Event()
    try:
        server = start_server(py, args.bind, args.bind_port)
        children.append(server)

        box_tls = bool(args.tls or args.tunnel)
        box_port = args.port if args.port is not None else args.bind_port
        box_host = args.host
        if args.tunnel:
            tunnel, box_host = start_cloudflared(args.bind_port)
            children.append(tunnel)
            box_port = 443
            box_tls = True
        ips = lan_ipv4s()
        if not box_host:
            if not ips:
                print(
                    "No LAN IPv4 found. Pass SERVER_HOST=... or TUNNEL=1.",
                    file=sys.stderr,
                )
                return 2
            box_host = ips[0]
        banner(ssid, args.bind, args.bind_port, box_host, box_port, box_tls, ips)

        local_base = f"http://127.0.0.1:{args.bind_port}"
        if not args.no_peer:
            threading.Thread(
                target=peer_loop, args=(local_base, token_b, stop), daemon=True
            ).start()
        threading.Thread(target=poll_status, args=(local_base, stop), daemon=True).start()

        if not args.skip_flash:
            rc = flash_h17(box_host, box_port, 1 if box_tls else 0, args.serial)
            if rc != 0:
                return rc
            print(
                "Flash ok. Watch [server] -- heartbeat box-a from <box-ip> "
                "and the LCD beat count. UART: make monitor",
                flush=True,
            )
        else:
            print("Skipping flash. Point an already-flashed box at this server.", flush=True)

        print("Heartbeat running. Ctrl-C to stop.", flush=True)
        while True:
            if server.poll() is not None:
                print("heartbeat server exited", file=sys.stderr)
                return server.returncode or 1
            time.sleep(0.4)
    except KeyboardInterrupt:
        print("\nstopped", flush=True)
        return 0
    finally:
        stop.set()
        for proc in reversed(children):
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == "__main__":
    sys.exit(main())
