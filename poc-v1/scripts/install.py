#!/usr/bin/env python3
"""Create a venv and install host tools used by the Makefile device tasks.

Cross-platform: macOS, Linux, and Windows. Prefers a repo-local .venv so
the rest of `make` does not depend on system site-packages.

ESP-IDF itself is not installed here; use `make idf-install` for compiler
toolchains. Firmware builds need IDF. `make install-server` adds FastAPI.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VENV = ROOT / ".venv"
REQUIREMENTS = ROOT / "requirements.txt"
REQUIREMENTS_SERVER = ROOT / "requirements-server.txt"
REQUIREMENTS_PERSONA = ROOT / "requirements-persona.txt"
MIN_PY = (3, 9)


def _venv_python() -> Path:
    if os.name == "nt":
        return VENV / "Scripts" / "python.exe"
    return VENV / "bin" / "python"


def _run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, check=False, text=True, **kwargs)


def _have(cmd: str) -> bool:
    return shutil.which(cmd) is not None


def _try_install_python() -> None:
    """Best-effort system Python from a package manager. Never required if
    the interpreter running this script is already new enough."""
    plat = sys.platform
    print("Python >= 3.9 is required. Trying a package manager…")
    if plat == "darwin" and _have("brew"):
        print("-> brew install python")
        _run(["brew", "install", "python"])
        return
    if plat.startswith("linux"):
        if _have("apt-get"):
            print("-> apt-get install -y python3 python3-venv python3-pip")
            cmd = ["apt-get", "install", "-y", "python3", "python3-venv", "python3-pip"]
            if os.geteuid() != 0 and _have("sudo"):
                cmd = ["sudo"] + cmd
            _run(cmd)
            return
        if _have("dnf"):
            print("-> dnf install -y python3 python3-pip")
            cmd = ["dnf", "install", "-y", "python3", "python3-pip"]
            if os.geteuid() != 0 and _have("sudo"):
                cmd = ["sudo"] + cmd
            _run(cmd)
            return
        if _have("pacman"):
            print("-> pacman -S --noconfirm python python-pip")
            cmd = ["pacman", "-S", "--noconfirm", "python", "python-pip"]
            if os.geteuid() != 0 and _have("sudo"):
                cmd = ["sudo"] + cmd
            _run(cmd)
            return
    if plat == "win32" and _have("winget"):
        print("-> winget install -e --id Python.Python.3.12")
        _run(["winget", "install", "-e", "--id", "Python.Python.3.12"])
        return
    print(
        "Could not install Python automatically.\n"
        "  macOS:  https://brew.sh then `brew install python`\n"
        "  Linux:  python3 python3-venv python3-pip from your distro\n"
        "  Windows: https://www.python.org/downloads/ or `winget install Python.Python.3.12`"
    )


def _ensure_venv_module(py: str) -> None:
    probe = _run([py, "-c", "import venv, ensurepip"], capture_output=True)
    if probe.returncode == 0:
        return
    print("Python venv/ensurepip missing; installing distro packages…")
    if sys.platform.startswith("linux") and _have("apt-get"):
        cmd = ["apt-get", "install", "-y", "python3-venv", "python3-pip"]
        if os.geteuid() != 0 and _have("sudo"):
            cmd = ["sudo"] + cmd
        rc = _run(cmd).returncode
        if rc != 0:
            sys.exit("Failed to install python3-venv. Install it and re-run make install.")
        return
    sys.exit(
        "This Python cannot create a venv.\n"
        "  Debian/Ubuntu: sudo apt-get install python3-venv python3-pip\n"
        "  Then re-run: make install"
    )


def main() -> int:
    install_server = "--server" in sys.argv
    install_persona = "--persona" in sys.argv
    print("== install")
    print(f"repo:     {ROOT}")
    print(f"platform: {sys.platform} ({os.name})")
    print(f"python:   {sys.executable} ({sys.version.split()[0]})")

    if sys.version_info < MIN_PY:
        _try_install_python()
        sys.exit(
            f"Need Python {MIN_PY[0]}.{MIN_PY[1]}+; this interpreter is "
            f"{sys.version_info.major}.{sys.version_info.minor}. "
            "Re-run make install with python3."
        )

    if not REQUIREMENTS.is_file():
        sys.exit(f"missing {REQUIREMENTS}")

    _ensure_venv_module(sys.executable)

    print(f"-> python -m venv {VENV}")
    created = _run([sys.executable, "-m", "venv", str(VENV)])
    if created.returncode != 0:
        sys.exit("venv creation failed")

    py = _venv_python()
    if not py.is_file():
        sys.exit(f"venv python not found at {py}")

    print("-> python -m pip install --upgrade pip")
    pip_up = _run([str(py), "-m", "pip", "install", "--upgrade", "pip"])
    if pip_up.returncode != 0:
        sys.exit("pip upgrade failed (network?)")

    print(f"-> python -m pip install -r {REQUIREMENTS.name}")
    pkgs = _run([str(py), "-m", "pip", "install", "-r", str(REQUIREMENTS)])
    if pkgs.returncode != 0:
        sys.exit(
            "pip install failed. Check the network, then retry `make install`.\n"
            "Manual: https://pypi.org/project/esptool/"
        )

    if install_server:
        if not REQUIREMENTS_SERVER.is_file():
            sys.exit(f"missing {REQUIREMENTS_SERVER}")
        print(f"-> python -m pip install -r {REQUIREMENTS_SERVER.name}")
        srv = _run([str(py), "-m", "pip", "install", "-r", str(REQUIREMENTS_SERVER)])
        if srv.returncode != 0:
            sys.exit("pip install of server demo deps failed (network?)")

    if install_persona:
        if not REQUIREMENTS_PERSONA.is_file():
            sys.exit(f"missing {REQUIREMENTS_PERSONA}")
        print(f"-> python -m pip install -r {REQUIREMENTS_PERSONA.name}")
        per = _run([str(py), "-m", "pip", "install", "-r", str(REQUIREMENTS_PERSONA)])
        if per.returncode != 0:
            sys.exit("pip install of persona-asset demo deps failed (network?)")

    ver = _run(
        [str(py), "-c", "import esptool, serial; print('esptool', getattr(esptool, '__version__', '?')); print('pyserial', serial.__version__)"],
        capture_output=True,
    )
    if ver.returncode == 0:
        print(ver.stdout.rstrip())

    if sys.platform.startswith("linux"):
        print()
        print("Linux note: the BOX-3 USB Serial/JTAG is VID 303A.")
        print("If `make connect` gets permission denied, add a udev rule")
        print("for idVendor=303a and add your user to the dialout/uucp group.")

    print()
    print("Installed. Next: make connect")
    if install_server:
        print("Server demos: make demo-auth")
    if install_persona:
        print("Persona assets: make demos-persona")
    return 0


if __name__ == "__main__":
    sys.exit(main())
