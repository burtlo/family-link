#!/usr/bin/env python3
"""Create repo .venv, host USB Python deps, and ESP-IDF (esp32s3 toolchain).

Invoked by `make install` (`scripts/make/python.sh host-run`) with a *host*
Python (python3 on PATH) before `.venv` exists.

Defaults: project.defaults.ini at repo root (see scripts/project_config.py).
Every setting may be overridden by its uppercase qualified environment name.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

from project_config import ProjectConfig, ProjectConfigError

ROOT = Path(__file__).resolve().parent.parent

EXIT_OK = 0


class InstallError(ProjectConfigError):
    """Install step failed."""


def _venv_python(venv: Path) -> Path:
    if os.name == "nt":
        return venv / "Scripts" / "python.exe"
    return venv / "bin" / "python"


def _run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, check=False, text=True, **kwargs)


def _require_rc(rc: int, message: str) -> None:
    if rc != 0:
        raise InstallError(message)


def _have(cmd: str) -> bool:
    return shutil.which(cmd) is not None


def _idf_tree_present(dest: Path) -> bool:
    return (dest / "export.sh").is_file() or (dest / "export.bat").is_file()


def _idf_clone(dest: Path, config: ProjectConfig) -> None:
    if not _have("git"):
        raise InstallError("git is required to clone ESP-IDF. Install Git and re-run make install.")
    dest.parent.mkdir(parents=True, exist_ok=True)
    version = config.text("idf.version", required=True)
    print(f"-> git clone --progress --depth 1 --branch {version} → {dest}")
    _require_rc(
        _run(
            [
                "git",
                "clone",
                "--progress",
                "--depth",
                "1",
                "--branch",
                version,
                "https://github.com/espressif/esp-idf.git",
                str(dest),
            ]
        ).returncode,
        "git clone of esp-idf failed (network / disk?)",
    )
    print("-> git submodule update --init --depth 1 --progress")
    _require_rc(
        _run(
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
        ).returncode,
        "esp-idf submodule update failed",
    )


def _idf_install_tools(dest: Path, config: ProjectConfig) -> None:
    target = config.text("idf.target", required=True)
    if os.name == "nt":
        install = dest / "install.bat"
        if not install.is_file():
            raise InstallError(f"no install.bat in {dest}")
        print(f"-> install.bat {target} (downloads toolchain; may take several minutes)")
        _require_rc(
            _run(["cmd", "/c", str(install), target], cwd=str(dest)).returncode,
            "IDF install.bat failed",
        )
        return
    install = dest / "install.sh"
    if not install.is_file():
        raise InstallError(f"no install.sh in {dest}")
    print(f"-> ./install.sh {target} (downloads toolchain; may take several minutes)")
    _require_rc(
        _run(["bash", str(install), target], cwd=str(dest)).returncode,
        "IDF install.sh failed",
    )


def ensure_esp_idf(config: ProjectConfig) -> Path:
    """Clone ESP-IDF if missing; run Espressif install script for the SoC target."""
    dest = config.path_value("idf.path")
    if config.boolean("idf.skip"):
        print("-> ESP-IDF skipped (IDF_SKIP=true)")
        return dest

    present = _idf_tree_present(dest)
    reinstall = config.boolean("idf.reinstall")

    print()
    print("== esp-idf")
    print(f"dest:    {dest}")
    print(f"version: {config.text('idf.version', required=True)}")
    print(f"target:  {config.text('idf.target', required=True)}")

    if not present:
        _idf_clone(dest, config)
        _idf_install_tools(dest, config)
    elif reinstall:
        print("-> re-running tool install (IDF_REINSTALL=true)")
        _idf_install_tools(dest, config)
    else:
        print("-> ESP-IDF tree already present; skipping clone and tool install")
        print("   (set IDF_REINSTALL=true to re-run install.sh)")

    print(f"-> IDF_PATH={dest}")
    return dest


def _run_pkg_install(cmd: list[str], label: str) -> None:
    print(f"-> {' '.join(cmd)}")
    _require_rc(_run(cmd).returncode, f"{label} failed (exit non-zero)")


def _try_install_python(minimum: tuple[int, ...]) -> None:
    """Best-effort system Python from a package manager. Caller must still re-run install."""
    plat = sys.platform
    required = ".".join(str(part) for part in minimum)
    print(f"Python >= {required} is required. Trying a package manager…")
    if plat == "darwin" and _have("brew"):
        _run_pkg_install(["brew", "install", "python"], "brew install python")
        return
    if plat.startswith("linux"):
        if _have("apt-get"):
            cmd = ["apt-get", "install", "-y", "python3", "python3-venv", "python3-pip"]
            if os.geteuid() != 0 and _have("sudo"):
                cmd = ["sudo"] + cmd
            _run_pkg_install(cmd, "apt-get install python3")
            return
        if _have("dnf"):
            cmd = ["dnf", "install", "-y", "python3", "python3-pip"]
            if os.geteuid() != 0 and _have("sudo"):
                cmd = ["sudo"] + cmd
            _run_pkg_install(cmd, "dnf install python3")
            return
        if _have("pacman"):
            cmd = ["pacman", "-S", "--noconfirm", "python", "python-pip"]
            if os.geteuid() != 0 and _have("sudo"):
                cmd = ["sudo"] + cmd
            _run_pkg_install(cmd, "pacman install python")
            return
    if plat == "win32" and _have("winget"):
        _run_pkg_install(
            ["winget", "install", "-e", "--id", "Python.Python.3.12"],
            "winget install Python",
        )
        return
    raise InstallError(
        "Could not install Python automatically.\n"
        "  macOS:  https://brew.sh then `brew install python`\n"
        "  Linux:  python3 python3-venv python3-pip from your distro\n"
        "  Windows: https://www.python.org/downloads/ or `winget install Python.Python.3.12`"
    )


def _venv_import_ok(py: str) -> bool:
    return _run([py, "-c", "import venv, ensurepip"], capture_output=True).returncode == 0


def _ensure_venv_module(py: str) -> None:
    if _venv_import_ok(py):
        return
    print("Python venv/ensurepip missing; installing distro packages…")
    if sys.platform.startswith("linux") and _have("apt-get"):
        cmd = ["apt-get", "install", "-y", "python3-venv", "python3-pip"]
        if os.geteuid() != 0 and _have("sudo"):
            cmd = ["sudo"] + cmd
        _require_rc(_run(cmd).returncode, "Failed to install python3-venv. Install it and re-run make install.")
        if not _venv_import_ok(py):
            raise InstallError(
                "python3-venv was installed but this interpreter still cannot import venv. "
                "Re-run make install with python3 from your distro."
            )
        return
    raise InstallError(
        "This Python cannot create a venv.\n"
        "  Debian/Ubuntu: sudo apt-get install python3-venv python3-pip\n"
        "  Then re-run: make install"
    )


def _install_host_packages(py: Path, requirements: Path) -> None:
    print("-> python -m pip install --upgrade pip")
    _require_rc(
        _run([str(py), "-m", "pip", "install", "--upgrade", "pip"]).returncode,
        "pip upgrade failed (network?)",
    )

    print(f"-> python -m pip install -r {requirements.name}")
    _require_rc(
        _run([str(py), "-m", "pip", "install", "-r", str(requirements)]).returncode,
        "pip install failed. Check the network, then retry `make install`.\n"
        "Manual: https://pypi.org/project/esptool/",
    )

    ver = _run(
        [
            str(py),
            "-c",
            "import esptool, serial; print('esptool', getattr(esptool, '__version__', '?')); "
            "print('pyserial', serial.__version__)",
        ],
        capture_output=True,
    )
    if ver.returncode != 0:
        detail = (ver.stderr or ver.stdout or "").strip()
        msg = "Installed packages failed import check (esptool, pyserial)."
        if detail:
            msg = f"{msg}\n{detail}"
        raise InstallError(msg)
    print(ver.stdout.rstrip())


def run_install(config: ProjectConfig) -> None:
    config.validate()
    minimum_python = config.version_tuple("python.minimum")
    venv = config.path_value("python.venv")
    requirements = config.path_value("python.requirements")

    print("== install")
    print(f"repo:     {ROOT}")
    print(f"platform: {sys.platform} ({os.name})")
    print(f"python:   {sys.executable} ({sys.version.split()[0]})")

    if sys.version_info < minimum_python:
        _try_install_python(minimum_python)
        required = ".".join(str(part) for part in minimum_python)
        raise InstallError(
            f"Need Python {required}+; this interpreter is "
            f"{sys.version_info.major}.{sys.version_info.minor}. "
            "Re-run make install with python3."
        )

    if not requirements.is_file():
        raise InstallError(f"missing {requirements}")

    _ensure_venv_module(sys.executable)

    print(f"-> python -m venv {venv}")
    _require_rc(
        _run([sys.executable, "-m", "venv", str(venv)]).returncode,
        "venv creation failed",
    )

    py = _venv_python(venv)
    if not py.is_file():
        raise InstallError(f"venv python not found at {py}")

    _install_host_packages(py, requirements)
    ensure_esp_idf(config)

    if sys.platform.startswith("linux"):
        print()
        print("Linux note: Espressif USB Serial/JTAG is VID 303A.")
        print("If serial open fails with permission denied, add a udev rule")
        print("for idVendor=303a and add your user to the dialout/uucp group.")

    print()
    print("Installed. Next: make help")
    print("Firmware builds: set IDF_PATH (see above) and use ESP-IDF export in your shell.")


def main() -> int:
    try:
        run_install(ProjectConfig())
    except ProjectConfigError as exc:
        print(exc, file=sys.stderr)
        return exc.exit_code
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
