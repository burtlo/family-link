"""Build immutable bootstrap packages, or flash an existing package explicitly."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

from project_config import ProjectConfig, ProjectConfigError, ROOT


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(argv, *, cwd=ROOT, env=None, timeout=900):
    subprocess.run([str(a) for a in argv], cwd=cwd, env=env, timeout=timeout, check=True)


def idf_environment(config):
    idf = config.path_value("idf.path")
    env = dict(os.environ, IDF_PATH=str(idf))
    if os.name == "nt":
        env = {key.upper(): value for key, value in env.items()}
        # Make recipes enter through Git Bash; export.bat requires a native CMD environment.
        env.pop("MSYSTEM", None)
        export = idf / "export.bat"
        command = f'call "{export}" && echo __FAMILY_IDF_ENV__ && set'
        if '"' in str(export) or '\n' in str(export) or '%' in str(export):
            raise ProjectConfigError("Unsupported character in IDF path")
        # CMD /s needs outer quotes around the entire command, not C-runtime escaped quotes.
        result = subprocess.run(f'cmd /d /s /c "{command}"', env=env,
                                capture_output=True, text=True, timeout=120)
        if result.returncode or "__FAMILY_IDF_ENV__" not in result.stdout:
            raise ProjectConfigError(f"ESP-IDF activation failed:\n{result.stdout}\n{result.stderr}")
        for line in result.stdout.split("__FAMILY_IDF_ENV__", 1)[1].splitlines():
            key, sep, value = line.partition("=")
            if sep and key:
                env[key] = value
    else:
        export = idf / "export.sh"
        result = subprocess.run(["bash", "-c", f"source {shlex.quote(str(export))} >/dev/null && env -0"],
                                env=env, capture_output=True, check=True, timeout=120)
        for entry in result.stdout.decode().split("\0"):
            key, sep, value = entry.partition("=")
            if sep:
                env[key] = value
    python = shutil.which("python", path=env.get("PATH"))
    if not python:
        raise ProjectConfigError("ESP-IDF Python unavailable; run make install")
    result = subprocess.run(["git", "describe", "--tags", "--exact-match"], cwd=idf,
                            capture_output=True, text=True, timeout=30)
    if result.returncode or result.stdout.strip() != config.text("idf.version", required=True):
        raise ProjectConfigError("ESP-IDF checkout does not match configured idf.version")
    return env, [python, str(idf / "tools" / "idf.py")]


def publish(staging, artifacts, build_id):
    destination = artifacts / build_id
    staging.rename(destination)
    pointer = artifacts / "current.tmp"
    pointer.write_text(json.dumps({"package": build_id}) + "\n", encoding="utf-8")
    os.replace(pointer, artifacts / "current.json")
    return destination


def build(config):
    if config.text("idf.target", required=True) != "esp32s3":
        raise ProjectConfigError("Bootstrap currently supports esp32s3 only")
    env, idf = idf_environment(config)
    source = config.path_value("firmware.source")
    output = config.path_value("firmware.build")
    artifacts = config.path_value("firmware.artifacts")
    output.mkdir(parents=True, exist_ok=True)
    artifacts.mkdir(parents=True, exist_ok=True)
    build_id = uuid.uuid4().hex
    args = idf + ["-C", str(source), "-B", str(output), "-DIDF_TARGET=esp32s3",
                  f"-DSDKCONFIG={output / 'sdkconfig'}", f"-DFAMILY_BUILD_ID={build_id}"]
    timeout = config.integer("command.build.timeout_seconds", default=900, minimum=1)
    run(args + ["build"], env=env, timeout=timeout)
    run(args + ["merge-bin", "-o", str(output / "firmware.bin")], env=env, timeout=timeout)
    run(args + ["size", "--format", "json", "--output-file", str(output / "size.json")],
        env=env, timeout=timeout)
    metadata = json.loads((output / "flasher_args.json").read_text())
    with tempfile.TemporaryDirectory(prefix=".package-", dir=artifacts) as directory:
        staging = Path(directory) / "package"
        staging.mkdir()
        images = []
        for index, (offset, filename) in enumerate(metadata["flash_files"].items()):
            original = (output / filename).resolve()
            if not original.is_relative_to(output):
                raise ProjectConfigError("Flash metadata points outside build directory")
            name = f"{index}-{original.name}"
            shutil.copyfile(original, staging / name)
            images.append({"offset": offset, "file": name})
        for name in ("firmware.bin", "size.json", "sdkconfig", "flasher_args.json"):
            shutil.copyfile(output / name, staging / name)
        shutil.copyfile(output / "family_link_bootstrap.elf", staging / "firmware.elf")
        shutil.copyfile(source / "partitions.csv", staging / "partitions.csv")
        manifest = {"document": "family-link.firmware-package/v1", "build_id": build_id,
                    "created_at": datetime.now(timezone.utc).isoformat(), "target": "esp32s3",
                    "idf_version": config.text("idf.version"), "images": images,
                    "flash_settings": metadata["flash_settings"],
                    "files": {p.name: digest(p) for p in staging.iterdir()}}
        (staging / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        package = publish(staging, artifacts, build_id)
    print(f"Built {package / 'firmware.bin'}\nBuild ID: {build_id}")


def package_for(config):
    artifacts = config.path_value("firmware.artifacts")
    try:
        pointer = json.loads((artifacts / "current.json").read_text())
        package = (artifacts / pointer["package"]).resolve()
        if package.parent != artifacts:
            raise ProjectConfigError("Invalid package pointer")
        manifest = json.loads((package / "manifest.json").read_text())
        if manifest["document"] != "family-link.firmware-package/v1":
            raise ProjectConfigError("Unsupported firmware package")
        if manifest["target"] != config.text("idf.target") or manifest["idf_version"] != config.text("idf.version"):
            raise ProjectConfigError("Package target/toolchain differs from configured build; run make build")
        for name, expected in manifest["files"].items():
            path = (package / name).resolve()
            if path.parent != package or digest(path) != expected:
                raise ProjectConfigError(f"Package integrity failed: {name}")
        for image in manifest["images"]:
            if image["file"] not in manifest["files"]:
                raise ProjectConfigError("Unverified image in package")
        return package, manifest
    except (OSError, ValueError, KeyError) as exc:
        raise ProjectConfigError("No valid firmware package; run make build") from exc


def capture(port, build_id, log_path, seconds):
    import serial
    marker = f"V2_BOOTSTRAP_READY build={build_id}".encode()
    deadline = time.monotonic() + seconds
    data = bytearray()
    with serial.Serial(port, 115200, timeout=0.25) as connection, log_path.open("wb") as log:
        connection.dtr = False
        connection.rts = False
        while time.monotonic() < deadline:
            chunk = connection.read(2048)
            log.write(chunk)
            data.extend(chunk)
            if marker in data:
                return True
            # Bound host memory even when the port is noisy; retain marker overlap.
            if len(data) > 8192:
                del data[:-4096]
    return False


def wait_for_kit(kit, seconds):
    from device_targets import ports, resolve_port
    deadline = time.monotonic() + seconds
    while True:
        try:
            return resolve_port(kit, ports())
        except ProjectConfigError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.25)


def flash(config, name, all_kits):
    from deployment_config import load_active_deployment
    from device_targets import ports, select, resolve_port
    package, manifest = package_for(config)
    deployment = load_active_deployment(config)
    targets = select(deployment, ports(), name, all_kits)
    print(f"Package {manifest['build_id']}")
    for kit, port in targets:
        print(f"  {kit.id} -> {port.device}")
    logs = config.path_value("firmware.logs") / uuid.uuid4().hex
    logs.mkdir(parents=True)
    failed = False
    for kit, _ in targets:
        written = False
        try:
            port = resolve_port(kit, ports())  # Ports may change between kits.
            settings = manifest["flash_settings"]
            args = [sys.executable, "-m", "esptool", "--chip", manifest["target"], "--port", port.device,
                    "write-flash", "--flash-mode", settings["flash_mode"],
                    "--flash-freq", settings["flash_freq"], "--flash-size", settings["flash_size"]]
            for image in manifest["images"]:
                args.extend([image["offset"], str(package / image["file"])])
            run(args, timeout=config.integer("command.flash.timeout_seconds", default=180, minimum=1))
            written = True
            port = wait_for_kit(kit, config.integer("firmware.boot_seconds", default=12, minimum=1))
            if not capture(port.device, manifest["build_id"], logs / f"{kit.id}.log",
                           config.integer("firmware.boot_seconds", default=12, minimum=1)):
                raise ProjectConfigError("Expected bootstrap build marker not observed")
            print(f"PASS {kit.id}: written and boot confirmed")
        except (ProjectConfigError, OSError, subprocess.SubprocessError) as exc:
            failed = True
            print(f"FAIL {kit.id}: {'written; boot unconfirmed' if written else 'write unconfirmed'}: {exc}", file=sys.stderr)
    print(f"Boot evidence: {logs}")
    if failed:
        raise ProjectConfigError("One or more kits failed; see per-kit results")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["build", "devices", "flash", "bind"])
    parser.add_argument("--kit", default="")
    parser.add_argument("--port", default="")
    parser.add_argument("--all", action="store_true")
    args = parser.parse_args()
    try:
        config = ProjectConfig()
        config.validate()
        if args.action == "build":
            build(config)
        elif args.action == "flash":
            flash(config, args.kit, args.all)
        else:
            from deployment_config import load_active_deployment
            from device_targets import ports, bind, resolve_port, validate_bindings
            deployment = load_active_deployment(config)
            available = ports()
            if args.action == "bind":
                bind(deployment, args.kit, args.port, available, project=config)
            else:
                validate_bindings(deployment)
                for kit in deployment.kits:
                    aliases = ",".join(a for a, e in deployment.aliases.items() if e == kit.endpoint_id)
                    try:
                        status = resolve_port(kit, available).device
                    except ProjectConfigError as exc:
                        status = str(exc)
                    print(f"{aliases or kit.id}: {kit.id}: {status}")
                print("Detected ports:")
                for port in available:
                    print(f"  {port.device} usb_serial={port.serial or '(unreadable)'}")
        return 0
    except (ProjectConfigError, OSError, subprocess.SubprocessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
