#!/usr/bin/env python3
"""Create repo .venv, host USB Python deps, and ESP-IDF (esp32s3 toolchain).

Invoked by `make install` (`scripts/make/python.sh host-run`) with a *host*
Python (python3 on PATH) before `.venv` exists.

Environment:
  FAMILY_IDF_DIR      Clone/install tree (default: ~/esp/esp-idf)
  FAMILY_IDF_VERSION  ESP-IDF git branch/tag (default: v5.4.2)
  FAMILY_IDF_SKIP=1   Skip ESP-IDF (venv + pip only)
  FAMILY_IDF_REINSTALL=1  Re-run install.sh/bat esp32s3 even if IDF tree exists
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
MIN_PY = (3, 9)

IDF_CLONE_DEFAULT = Path.home() / "esp" / "esp-idf"
IDF_VERSION = os.environ.get("FAMILY_IDF_VERSION", "v5.4.2")
IDF_TARGET = os.environ.get("FAMILY_IDF_TARGET", "esp32s3")


def _venv_python() -> Path:
    if os.name == "nt":
        return VENV / "Scripts" / "python.exe"
    return VENV / "bin" / "python"


def _run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, check=False, text=True, **kwargs)


def _have(cmd: str) -> bool:
    return shutil.which(cmd) is not None


def _idf_dest() -> Path:
    raw = os.environ.get("FAMILY_IDF_DIR") or os.environ.get("IDF_PATH")
    if raw and str(raw).strip():
        return Path(raw).expanduser().resolve()
    return IDF_CLONE_DEFAULT.expanduser().resolve()


def _idf_tree_present(dest: Path) -> bool:
    return (dest / "export.sh").is_file() or (dest / "export.bat").is_file()


def _idf_clone(dest: Path) -> None:
    if not _have("git"):
        sys.exit("git is required to clone ESP-IDF. Install Git and re-run make install.")
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f"-> git clone --progress --depth 1 --branch {IDF_VERSION} → {dest}")
    rc = _run(
        [
            "git",
            "clone",
            "--progress",
            "--depth",
            "1",
            "--branch",
            IDF_VERSION,
            "https://github.com/espressif/esp-idf.git",
            str(dest),
        ]
    ).returncode
    if rc != 0:
        sys.exit("git clone of esp-idf failed (network / disk?)")
    print("-> git submodule update --init --depth 1 --progress")
    rc = _run(
        [
            "git",
            "submodule",
            "update",
            "--init",
            "--depth",
            "1",
            "--progress",
            "--jobs",
            "8",
        ],
        cwd=str(dest),
    ).returncode
    if rc != 0:
        sys.exit("esp-idf submodule update failed")


def _idf_install_tools(dest: Path) -> None:
    if os.name == "nt":
        install = dest / "install.bat"
        if not install.is_file():
            sys.exit(f"no install.bat in {dest}")
        print(f"-> install.bat {IDF_TARGET} (downloads toolchain; may take several minutes)")
        rc = _run(["cmd", "/c", str(install), IDF_TARGET], cwd=str(dest)).returncode
        if rc != 0:
            sys.exit("IDF install.bat failed")
        return
    install = dest / "install.sh"
    if not install.is_file():
        sys.exit(f"no install.sh in {dest}")
    print(f"-> ./install.sh {IDF_TARGET} (downloads toolchain; may take several minutes)")
    rc = _run(["bash", str(install), IDF_TARGET], cwd=str(dest)).returncode
    if rc != 0:
        sys.exit("IDF install.sh failed")


def ensure_esp_idf() -> Path:
    """Clone ESP-IDF if missing; run Espressif install script for the SoC target."""
    if os.environ.get("FAMILY_IDF_SKIP") == "1":
        print("-> ESP-IDF skipped (FAMILY_IDF_SKIP=1)")
        return _idf_dest()

    dest = _idf_dest()
    present = _idf_tree_present(dest)
    reinstall = os.environ.get("FAMILY_IDF_REINSTALL") == "1"

    print()
    print("== esp-idf")
    print(f"dest:    {dest}")
    print(f"version: {IDF_VERSION}")
    print(f"target:  {IDF_TARGET}")

    if not present:
        _idf_clone(dest)
        _idf_install_tools(dest)
    elif reinstall:
        print("-> re-running tool install (FAMILY_IDF_REINSTALL=1)")
        _idf_install_tools(dest)
    else:
        print("-> ESP-IDF tree already present; skipping clone and tool install")
        print("   (set FAMILY_IDF_REINSTALL=1 to re-run install.sh)")

    print(f"-> IDF_PATH={dest}")
    return dest


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

    ver = _run(
        [str(py), "-c", "import esptool, serial; print('esptool', getattr(esptool, '__version__', '?')); print('pyserial', serial.__version__)"],
        capture_output=True,
    )
    if ver.returncode == 0:
        print(ver.stdout.rstrip())

    ensure_esp_idf()

    if sys.platform.startswith("linux"):
        print()
        print("Linux note: Espressif USB Serial/JTAG is VID 303A.")
        print("If serial open fails with permission denied, add a udev rule")
        print("for idVendor=303a and add your user to the dialout/uucp group.")

    print()
    print("Installed. Next: make help")
    print("Firmware builds: set IDF_PATH (see above) and use ESP-IDF export in your shell.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
