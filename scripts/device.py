#!/usr/bin/env python3
"""BOX-3 inspect commands used by the Makefile.

Small tasks are independent. Meta tasks (connect, system, storage) run the
matching small tasks and print their names so later sessions can ask about
a specific step.
"""

from __future__ import annotations

import argparse
import glob
import os
import platform
import struct
import sys
import time
from contextlib import ExitStack
from dataclasses import dataclass
from typing import Callable, Iterable

# Espressif USB Serial/JTAG (ESP32-S3 native USB). Also accept common
# USB-UART bridges in case a different cable is used later.
ESPRESSIF_VID = 0x303A
ESPRESSIF_JTAG_PID = 0x1001
BRIDGE_VIDS = {0x303A, 0x10C4, 0x1A86, 0x0403}

PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_SIZE = 0xC00
PARTITION_MAGIC = 0x50AA
PARTITION_MD5_MAGIC = 0xEBEB
APP_DESC_MAGIC = 0xABCD5432
BOOTLOADER_OFFSET = 0x0

DATA_SUBTYPES = {
    0x00: "otadata",
    0x01: "phy",
    0x02: "nvs",
    0x03: "coredump",
    0x04: "nvs_keys",
    0x05: "efuse_em",
    0x06: "undefined",
    0x80: "esphttpd",
    0x81: "fat",
    0x82: "spiffs",
    0x83: "littlefs",
}
APP_SUBTYPES = {
    0x00: "factory",
    0x20: "test",
}


class TaskError(Exception):
    """Reported failure for a device task (exit 1)."""


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def header(name: str) -> None:
    print(f"-- {name}")


def kv(key: str, value: object, indent: int = 1) -> None:
    pad = "  " * indent
    if value is None or value == "":
        value = "(none)"
    print(f"{pad}{key}: {value}")


def ok(msg: str) -> None:
    print(f"  [ok] {msg}")


def warn(msg: str) -> None:
    print(f"  [warn] {msg}")


def fail(msg: str) -> None:
    print(f"  [fail] {msg}")


def _cstr(data: bytes) -> str:
    return data.split(b"\x00", 1)[0].decode("utf-8", errors="replace").strip()


def _hex_mac(raw: bytes | Iterable[int]) -> str:
    b = bytes(raw)
    return ":".join(f"{x:02x}" for x in b)


def _env_port() -> str | None:
    for key in ("ESPPORT", "PORT"):
        val = os.environ.get(key)
        if val:
            return val
    return None


# ---------------------------------------------------------------------------
# USB / serial discovery (stdlib first; pyserial when installed)
# ---------------------------------------------------------------------------

@dataclass
class SerialDevice:
    port: str
    vid: int | None = None
    pid: int | None = None
    manufacturer: str | None = None
    product: str | None = None
    serial: str | None = None
    description: str | None = None

    @property
    def is_espressif(self) -> bool:
        return self.vid == ESPRESSIF_VID

    @property
    def likely_box(self) -> bool:
        if self.vid in BRIDGE_VIDS:
            return True
        name = (self.port or "").lower()
        return "usbmodem" in name or "ttyacm" in name


def _pyserial_ports() -> list[SerialDevice] | None:
    try:
        from serial.tools import list_ports
    except ImportError:
        return None
    found: list[SerialDevice] = []
    for info in list_ports.comports():
        if not info.device:
            continue
        # macOS: prefer cu.* (call-out) over tty.* (call-in)
        if "/dev/tty." in info.device.replace("\\", "/"):
            continue
        found.append(
            SerialDevice(
                port=info.device,
                vid=info.vid,
                pid=info.pid,
                manufacturer=info.manufacturer,
                product=info.product,
                serial=info.serial_number,
                description=info.description,
            )
        )
    return found


def _glob_ports() -> list[SerialDevice]:
    patterns: list[str]
    if sys.platform == "darwin":
        patterns = [
            "/dev/cu.usbmodem*",
            "/dev/cu.usbserial*",
            "/dev/cu.SLAB_USBtoUART*",
            "/dev/cu.wchusbserial*",
            "/dev/cu.usbmodem*",
        ]
    elif sys.platform.startswith("linux"):
        patterns = ["/dev/ttyACM*", "/dev/ttyUSB*"]
    else:
        return []
    ports: list[SerialDevice] = []
    seen: set[str] = set()
    for pat in patterns:
        for path in sorted(glob.glob(pat)):
            if path not in seen:
                seen.add(path)
                ports.append(SerialDevice(port=path))
    return ports


def _windows_com_ports() -> list[SerialDevice]:
    if os.name != "nt":
        return []
    ports: list[SerialDevice] = []
    try:
        from serial.tools import list_ports

        for info in list_ports.comports():
            ports.append(
                SerialDevice(
                    port=info.device,
                    vid=info.vid,
                    pid=info.pid,
                    manufacturer=info.manufacturer,
                    product=info.product,
                    serial=info.serial_number,
                    description=info.description,
                )
            )
        return ports
    except ImportError:
        pass
    try:
        import subprocess

        raw = subprocess.run(
            [
                "powershell",
                "-NoProfile",
                "-Command",
                "Get-CimInstance Win32_SerialPort | "
                "Select-Object -ExpandProperty DeviceID",
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        for line in raw.stdout.splitlines():
            line = line.strip()
            if line:
                ports.append(SerialDevice(port=line))
    except OSError:
        pass
    return ports


def list_serial_devices() -> list[SerialDevice]:
    found = _pyserial_ports()
    if found is not None:
        return found
    if os.name == "nt":
        return _windows_com_ports()
    return _glob_ports()


def candidate_devices() -> list[SerialDevice]:
    devices = list_serial_devices()
    espressif = [d for d in devices if d.is_espressif]
    if espressif:
        return espressif
    return [d for d in devices if d.likely_box]


def pick_port(devices: list[SerialDevice] | None = None) -> SerialDevice:
    forced = _env_port()
    deadline = time.time() + 8.0
    last: list[SerialDevice] = []
    while True:
        last = devices if devices is not None else candidate_devices()
        if forced:
            for d in last:
                if d.port == forced:
                    return d
            if os.name == "nt" or os.path.exists(forced):
                return SerialDevice(port=forced)
        if len(last) == 1:
            return last[0]
        if len(last) > 1:
            listed = ", ".join(d.port for d in last)
            raise TaskError(
                f"Several BOX-3 serial ports look plausible ({listed}). "
                "Re-run with PORT=/the/one make <task>."
            )
        if devices is not None or time.time() >= deadline:
            break
        time.sleep(0.25)
        devices = None
    raise TaskError(
        "No BOX-3 serial port found. Plug USB-C into the box "
        "(not the dock USB-C, which is power-only). "
        "If you just ran a task, tap Reset on the box and wait a second. "
        "Then: ls /dev/cu.usbmodem*  (macOS) or /dev/ttyACM* (Linux)."
    )


def print_device(dev: SerialDevice) -> None:
    kv("port", dev.port)
    if dev.vid is not None and dev.pid is not None:
        kv("usb", f"{dev.vid:04x}:{dev.pid:04x}")
    else:
        kv("usb", "(vid/pid unknown until pyserial is installed)")
    kv("manufacturer", dev.manufacturer)
    kv("product", dev.product)
    kv("serial", dev.serial)
    kv("description", dev.description)
    if dev.vid == ESPRESSIF_VID and dev.pid == ESPRESSIF_JTAG_PID:
        kv("interface", "Espressif USB Serial/JTAG (expected for BOX-3)")
    elif dev.is_espressif:
        kv("interface", "Espressif USB")
    elif dev.likely_box:
        kv("interface", "USB CDC ACM (likely native USB)")


# ---------------------------------------------------------------------------
# esptool session
# ---------------------------------------------------------------------------

def _require_esptool():
    try:
        import esptool
        from esptool.cmds import (
            attach_flash,
            detect_chip,
            detect_flash_size,
            read_flash,
            reset_chip,
            run_stub,
        )
        from esptool.logger import log
        from esptool.util import FatalError
    except ImportError as exc:
        raise TaskError(
            f"esptool import failed ({exc}). Run: make install"
        ) from exc
    try:
        log.set_verbosity("silent")
    except Exception:
        pass
    return (
        esptool,
        attach_flash,
        detect_chip,
        detect_flash_size,
        read_flash,
        reset_chip,
        run_stub,
        FatalError,
    )


class Bootloader:
    def __init__(self, reset_after: bool = True, flash: bool = False):
        self.reset_after = reset_after
        self.flash = flash
        self.esp = None
        self.port: str | None = None
        self._stack: ExitStack | None = None
        self._read_flash = None
        self._reset_chip = None
        self._detect_flash_size = None

    def __enter__(self):
        (
            _esptool,
            attach_flash,
            detect_chip,
            detect_flash_size,
            read_flash,
            reset_chip,
            run_stub,
            fatal_error,
        ) = _require_esptool()
        self._read_flash = read_flash
        self._reset_chip = reset_chip
        self._detect_flash_size = detect_flash_size
        chosen = pick_port()
        self.port = chosen.port
        self._stack = ExitStack()
        last_err: Exception | None = None
        esp = None
        modes = (
            ("usb-reset", "default-reset")
            if chosen.is_espressif
            else ("default-reset", "usb-reset")
        )
        for mode in modes:
            try:
                # detect_chip() already connects and returns a closeable ESPLoader.
                esp = detect_chip(port=self.port, connect_mode=mode)
                self._stack.enter_context(esp)
                last_err = None
                break
            except fatal_error as exc:
                last_err = exc
                if self._stack is not None:
                    self._stack.close()
                    self._stack = ExitStack()
                time.sleep(0.4)
        if esp is None:
            raise TaskError(
                f"ROM bootloader did not sync on {self.port}: {last_err}\n"
                "  Hold Boot, tap Reset, release Boot, then retry."
            ) from last_err
        esp = run_stub(esp)
        if self.flash:
            attach_flash(esp)
        self.esp = esp
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self.esp is not None and self.reset_after and self._reset_chip is not None:
            try:
                # watchdog-reset keeps USB-JTAG enumerated; RTS hard-reset can drop it.
                self._reset_chip(self.esp, "watchdog-reset")
            except Exception:
                try:
                    self._reset_chip(self.esp, "hard-reset")
                except Exception:
                    pass
        if self._stack is not None:
            self._stack.close()

    def read(self, address: int, size: int) -> bytes:
        data = self._read_flash(self.esp, address, size, no_progress=True)
        if data is None:
            raise TaskError(f"read-flash at 0x{address:x} returned no data")
        return data


def chip_summary(esp) -> dict:
    mac = None
    try:
        mac = _hex_mac(esp.read_mac())
    except Exception:
        mac = None
    features = []
    try:
        features = list(esp.get_chip_features() or [])
    except Exception:
        pass
    desc = None
    try:
        desc = esp.get_chip_description()
    except Exception:
        desc = getattr(esp, "CHIP_NAME", None)
    crystal = None
    try:
        crystal = esp.get_crystal_freq()
    except Exception:
        pass
    return {
        "chip": getattr(esp, "CHIP_NAME", None),
        "description": desc,
        "mac": mac,
        "features": features,
        "crystal_mhz": crystal,
    }


def print_chip(info: dict) -> None:
    kv("chip", info.get("chip"))
    kv("description", info.get("description"))
    kv("mac", info.get("mac"))
    feats = info.get("features") or []
    kv("features", ", ".join(str(f) for f in feats) if feats else "(unknown)")
    if info.get("crystal_mhz"):
        kv("crystal_mhz", info["crystal_mhz"])


def security_lines(esp) -> dict:
    info: dict = {}
    try:
        raw = esp.get_security_info()
    except Exception as exc:
        info["security"] = f"(unavailable: {exc})"
        return info
    if not isinstance(raw, dict):
        info["security"] = repr(raw)
        return info
    flags = raw.get("parsed_flags") or {}
    if isinstance(flags, dict):
        for key, value in flags.items():
            info[key] = value
    info["flash_crypt_cnt"] = raw.get("flash_crypt_cnt")
    info["chip_id"] = raw.get("chip_id")
    info["api_version"] = raw.get("api_version")
    return info


# ---------------------------------------------------------------------------
# Partition table + app descriptor
# ---------------------------------------------------------------------------

@dataclass
class Partition:
    name: str
    type: int
    subtype: int
    offset: int
    size: int
    flags: int = 0

    @property
    def type_name(self) -> str:
        if self.type == 0x00:
            return "app"
        if self.type == 0x01:
            return "data"
        return f"0x{self.type:02x}"

    @property
    def subtype_name(self) -> str:
        if self.type == 0x00:
            if 0x10 <= self.subtype <= 0x1F:
                return f"ota_{self.subtype - 0x10}"
            return APP_SUBTYPES.get(self.subtype, f"0x{self.subtype:02x}")
        if self.type == 0x01:
            return DATA_SUBTYPES.get(self.subtype, f"0x{self.subtype:02x}")
        return f"0x{self.subtype:02x}"

    @property
    def is_app(self) -> bool:
        return self.type == 0x00

    @property
    def is_fs(self) -> bool:
        return self.type == 0x01 and self.subtype in (0x81, 0x82, 0x83)

    @property
    def is_user_data(self) -> bool:
        return self.type == 0x01 and self.subtype in (
            0x02,  # nvs
            0x03,  # coredump
            0x81,
            0x82,
            0x83,
        )


def parse_partition_table(blob: bytes) -> tuple[list[Partition], str | None]:
    parts: list[Partition] = []
    md5_note = None
    offset = 0
    while offset + 32 <= len(blob):
        magic = struct.unpack_from("<H", blob, offset)[0]
        if magic == 0xFFFF or blob[offset : offset + 32] == b"\xff" * 32:
            break
        if magic == PARTITION_MD5_MAGIC:
            digest = blob[offset + 16 : offset + 32].hex()
            md5_note = digest
            break
        if magic != PARTITION_MAGIC:
            break
        ptype, subtype = blob[offset + 2], blob[offset + 3]
        poff, psize = struct.unpack_from("<II", blob, offset + 4)
        name = _cstr(blob[offset + 12 : offset + 28])
        flags = struct.unpack_from("<I", blob, offset + 28)[0]
        parts.append(Partition(name, ptype, subtype, poff, psize, flags))
        offset += 32
    return parts, md5_note


def find_app_desc(blob: bytes) -> dict | None:
    needle = struct.pack("<I", APP_DESC_MAGIC)
    idx = blob.find(needle)
    if idx < 0 or idx + 256 > len(blob):
        return None
    # esp_app_desc_t from ESP-IDF (magic already matched)
    version = _cstr(blob[idx + 16 : idx + 48])
    project = _cstr(blob[idx + 48 : idx + 80])
    time_s = _cstr(blob[idx + 80 : idx + 96])
    date_s = _cstr(blob[idx + 96 : idx + 112])
    idf_ver = _cstr(blob[idx + 112 : idx + 144])
    sha = blob[idx + 144 : idx + 176].hex()
    return {
        "project": project,
        "version": version,
        "idf": idf_ver,
        "built": f"{date_s} {time_s}".strip(),
        "elf_sha256": sha,
        "offset_in_image": f"0x{idx:x}",
    }


def find_bootloader_desc(blob: bytes) -> dict | None:
    """esp_bootloader_desc_t: magic 0x50, then version/idf/date strings."""
    # Scan 16-byte aligned records for a plausible descriptor.
    for idx in range(0, max(0, len(blob) - 96), 16):
        if blob[idx] != 0x50:
            continue
        version = _cstr(blob[idx + 4 : idx + 36])
        idf_ver = _cstr(blob[idx + 36 : idx + 68])
        date_time = _cstr(blob[idx + 68 : idx + 100])
        if idf_ver.startswith("v") and any(ch.isdigit() for ch in idf_ver):
            return {
                "version": version,
                "idf": idf_ver,
                "built": date_time,
                "offset_in_image": f"0x{idx:x}",
            }
    return None


FLASH_MFG = {
    0xC8: "GigaDevice",
    0xEF: "Winbond",
    0x20: "XMC",
    0x1C: "Eon",
    0xA1: "Fudan",
    0x1F: "Adesto",
}


def flash_info(esp) -> dict:
    info: dict = {}
    try:
        fid = esp.flash_id()
        if isinstance(fid, int):
            vendor = fid & 0xFF
            device = ((fid >> 16) & 0xFF) | (((fid >> 8) & 0xFF) << 8)
            info["flash_id"] = f"0x{fid:06x}"
            info["flash_mfg"] = f"0x{vendor:02x}" + (
                f" ({FLASH_MFG[vendor]})" if vendor in FLASH_MFG else ""
            )
            info["flash_device"] = f"0x{device:04x}"
    except Exception as exc:
        info["flash_id"] = f"(error: {exc})"
    try:
        ftype = esp.flash_type()
        info["flash_bus"] = {0: "quad SPI", 1: "octal SPI"}.get(ftype, ftype)
    except Exception:
        pass
    size_label = None
    try:
        from esptool.cmds import detect_flash_size

        size_label = detect_flash_size(esp)
    except Exception:
        size_label = None
    if isinstance(size_label, str) and size_label:
        info["flash_size"] = size_label
        info["flash_bytes"] = _parse_size_label(size_label)
    elif isinstance(size_label, int) and size_label > 0:
        info["flash_bytes"] = size_label
        info["flash_size"] = _fmt_bytes(size_label)
    return info


def _parse_size_label(label: str) -> int | None:
    text = label.strip().upper().replace(" ", "")
    try:
        if text.endswith("MB"):
            return int(float(text[:-2]) * 1024 * 1024)
        if text.endswith("KB"):
            return int(float(text[:-2]) * 1024)
        if text.endswith("B"):
            return int(float(text[:-1]))
        return int(text)
    except ValueError:
        return None


def _fmt_bytes(n: int) -> str:
    if n >= 1024 * 1024:
        return f"{n / (1024 * 1024):.1f} MB ({n} bytes)"
    if n >= 1024:
        return f"{n / 1024:.1f} KB ({n} bytes)"
    return f"{n} bytes"


# ---------------------------------------------------------------------------
# Filesystems
# ---------------------------------------------------------------------------

def erased(blob: bytes) -> bool:
    return blob and all(b == 0xFF for b in blob)


def fat_volume_and_files(head: bytes, read: Callable[[int, int], bytes], part: Partition) -> dict:
    if len(head) < 64:
        return {"kind": "unknown"}
    bytes_per_sec = struct.unpack_from("<H", head, 11)[0]
    sec_per_cluster = head[13]
    reserved = struct.unpack_from("<H", head, 14)[0]
    fats = head[16]
    root_entries = struct.unpack_from("<H", head, 17)[0]
    fat16_secs = struct.unpack_from("<H", head, 22)[0]
    total16 = struct.unpack_from("<H", head, 19)[0]
    total32 = struct.unpack_from("<I", head, 32)[0]
    fat32_secs = struct.unpack_from("<I", head, 36)[0] if len(head) >= 40 else 0
    if bytes_per_sec not in (512, 1024, 2048, 4096) or sec_per_cluster == 0:
        return {"kind": "unknown"}
    is_fat32 = fat16_secs == 0 and fat32_secs > 0
    kind = "FAT32" if is_fat32 else "FAT16/FAT12"
    total = total32 if total16 == 0 else total16
    label = _cstr(head[71:82]) if is_fat32 else _cstr(head[43:54])
    out = {
        "kind": kind,
        "volume_label": label or "(none)",
        "bytes_per_sector": bytes_per_sec,
        "sectors_per_cluster": sec_per_cluster,
        "total_sectors": total,
    }
    try:
        files = _fat_root_names(
            read,
            part.offset,
            bytes_per_sec,
            sec_per_cluster,
            reserved,
            fats,
            root_entries,
            fat16_secs,
            fat32_secs if is_fat32 else 0,
            is_fat32,
            head,
        )
        out["root_entries"] = files
    except Exception as exc:
        out["root_entries"] = f"(could not list: {exc})"
    return out


def _fat_root_names(
    read,
    part_off,
    bps,
    spc,
    reserved,
    fats,
    root_entries,
    fat16_secs,
    fat32_secs,
    is_fat32,
    head,
) -> list[str]:
    names: list[str] = []
    if is_fat32:
        root_cluster = struct.unpack_from("<I", head, 44)[0]
        fat_bytes = fats * fat32_secs * bps
        data_off = part_off + reserved * bps + fat_bytes
        cluster_off = data_off + (root_cluster - 2) * spc * bps
        blob = read(cluster_off, min(spc * bps, 4096))
    else:
        fat_bytes = fats * fat16_secs * bps
        root_off = part_off + reserved * bps + fat_bytes
        root_bytes = root_entries * 32
        blob = read(root_off, min(root_bytes, 4096))
    for i in range(0, len(blob) - 31, 32):
        ent = blob[i : i + 32]
        if ent[0] in (0x00, 0xE5) or ent[11] in (0x0F, 0x08):
            continue
        name = ent[0:8].decode("ascii", errors="ignore").strip()
        ext = ent[8:11].decode("ascii", errors="ignore").strip()
        full = f"{name}.{ext}" if ext else name
        if full and full != "." and full != "..":
            size = struct.unpack_from("<I", ent, 28)[0]
            names.append(f"{full} ({_fmt_bytes(size)})")
        if len(names) >= 32:
            names.append("…")
            break
    return names


def probe_region(head: bytes, subtype_name: str) -> dict:
    info: dict = {"subtype": subtype_name}
    if not head:
        info["kind"] = "empty-read"
        return info
    if erased(head):
        info["kind"] = "erased (0xFF)"
        info["populated"] = False
        return info
    info["populated"] = True
    if head[0:3] == b"\xEB" or (len(head) > 54 and (b"FAT" in head[54:62] or b"FAT" in head[82:90])):
        if b"FAT" in head[:90]:
            info["kind"] = "FAT (signature in boot sector)"
            return info
    if b"littlefs" in head[:4096]:
        info["kind"] = "LittleFS"
        return info
    # NVS page header: 32-byte header, version often 0xFE / 0xFF
    if subtype_name == "nvs":
        info["kind"] = "NVS (ESP-IDF key-value)"
        info["note"] = "Values not dumped (may include Wi-Fi secrets)."
        used_pages = 0
        # first 4k only; caller may pass more
        pages = max(1, len(head) // 4096)
        for i in range(pages):
            page = head[i * 4096 : (i + 1) * 4096]
            if page and not erased(page):
                used_pages += 1
        info["non_erased_pages_in_sample"] = used_pages
        return info
    if subtype_name == "spiffs":
        info["kind"] = "SPIFFS (partition subtype)"
        return info
    if subtype_name == "littlefs":
        info["kind"] = "LittleFS (partition subtype)"
        return info
    if subtype_name == "coredump":
        info["kind"] = "coredump region"
        return info
    if head[0] == 0xE9:
        info["kind"] = "ESP image (magic 0xE9)"
        return info
    info["kind"] = "data (unrecognized file tree)"
    return info


# ---------------------------------------------------------------------------
# Boot log
# ---------------------------------------------------------------------------

def wait_for_port(port: str, timeout: float = 8.0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.name == "nt":
            if any(d.port == port for d in list_serial_devices()):
                return True
        elif os.path.exists(port):
            return True
        time.sleep(0.2)
    return os.path.exists(port) if os.name != "nt" else any(
        d.port == port for d in list_serial_devices()
    )


def capture_boot_log(port: str, seconds: float = 2.5) -> str:
    try:
        import serial
    except ImportError as exc:
        raise TaskError("pyserial is not installed. Run: make install") from exc
    if not wait_for_port(port, timeout=8):
        return ""
    time.sleep(0.3)
    buf = bytearray()
    try:
        with serial.Serial(port, 115200, timeout=0.2) as ser:
            end = time.time() + seconds
            while time.time() < end:
                chunk = ser.read(4096)
                if chunk:
                    buf.extend(chunk)
    except Exception as exc:
        return f"(serial open failed: {exc})"
    text = buf.decode("utf-8", errors="replace")
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    return _strip_ansi(text)


def _strip_ansi(text: str) -> str:
    out = []
    i = 0
    while i < len(text):
        if text[i] == "\x1b" and i + 1 < len(text) and text[i + 1] == "[":
            i += 2
            while i < len(text) and text[i] not in "mKHfABCD":
                i += 1
            i += 1
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def parse_boot_log(text: str) -> dict:
    info: dict = {}
    if not text or text.startswith("(serial"):
        return info
    for line in text.splitlines():
        low = line.lower()
        if "esp-idf" in low and "idf" not in info:
            info["boot_idf_line"] = line.strip()
        if "chip is" in low and "chip_line" not in info:
            info["chip_line"] = line.strip()
        if "features:" in low and "features_line" not in info:
            info["features_line"] = line.strip()
        if "cpu_start" in low and "starting scheduler" in low:
            info["rtos"] = "FreeRTOS (scheduler started)"
    return info


# ---------------------------------------------------------------------------
# Small tasks
# ---------------------------------------------------------------------------

def cmd_device_detect() -> None:
    header("device-detect")
    devices = candidate_devices()
    if not devices:
        fail("no USB serial device that looks like the BOX-3")
        kv("hint", "USB-C on the box itself, not the dock")
        raise TaskError("device not connected")
    ok(f"{len(devices)} candidate serial device(s)")
    for i, dev in enumerate(devices, 1):
        kv(f"device[{i}]", dev.port)
        print_device(dev)
        print()


def cmd_device_port() -> None:
    header("device-port")
    dev = pick_port()
    ok(dev.port)
    print_device(dev)
    if _env_port():
        kv("source", "PORT/ESPPORT environment")
    else:
        kv("source", "auto")


def cmd_device_sync() -> None:
    header("device-sync")
    with Bootloader(reset_after=True, flash=False) as bl:
        name = getattr(bl.esp, "CHIP_NAME", "Espressif SoC")
        ok(f"ROM bootloader synced on {bl.port} ({name})")
        kv("stub", "flasher stub running")
        kv("communication", "SLIP download-mode session established")


def cmd_device_chip() -> None:
    header("device-chip")
    with Bootloader(reset_after=True, flash=False) as bl:
        info = chip_summary(bl.esp)
        ok(str(info.get("description") or info.get("chip")))
        print_chip(info)


def cmd_device_os() -> None:
    header("device-os")
    port = pick_port().port
    desc = None
    with Bootloader(reset_after=True, flash=True) as bl:
        parts, _ = _read_parts(bl)
        desc = _app_desc_from_parts(bl, parts)
        info = chip_summary(bl.esp)
    log = capture_boot_log(port)
    parsed = parse_boot_log(log)
    os_name = "ESP-IDF on FreeRTOS (Xtensa LX7)"
    kv("device_os", os_name)
    if desc and desc.get("idf"):
        kv("esp_idf", desc["idf"])
        ok(f"ESP-IDF {desc['idf']}")
    elif parsed.get("boot_idf_line"):
        kv("esp_idf_boot_log", parsed["boot_idf_line"])
        ok("IDF version seen on serial console")
    else:
        warn("IDF version not found in app descriptor or boot log")
    kv("rtos", parsed.get("rtos") or "FreeRTOS (ESP-IDF default)")
    kv("cpu", info.get("description") or "ESP32-S3")
    if parsed.get("chip_line"):
        kv("boot_log_chip", parsed["chip_line"])
    kv("host_os", f"{platform.system()} {platform.release()} ({platform.machine()})")


def cmd_device_software() -> None:
    header("device-software")
    with Bootloader(reset_after=True, flash=True) as bl:
        parts, _ = _read_parts(bl)
        app = _app_desc_from_parts(bl, parts)
        try:
            boot = find_bootloader_desc(bl.read(BOOTLOADER_OFFSET, 0x4000))
        except TaskError:
            boot = None
        host_esptool = _esptool_version()
    if app:
        ok(f"{app.get('project') or 'app'} {app.get('version') or ''}".strip())
        kv("project", app.get("project"))
        kv("app_version", app.get("version"))
        kv("esp_idf", app.get("idf"))
        kv("built", app.get("built"))
        kv("elf_sha256", app.get("elf_sha256"))
    else:
        warn("no esp_app_desc_t in the factory/ota image (encrypted, empty, or non-IDF)")
    if boot:
        kv("bootloader_version", boot.get("version"))
        kv("bootloader_idf", boot.get("idf"))
        kv("bootloader_built", boot.get("built"))
    kv("host_esptool", host_esptool)
    kv("note", "Stock BOX-3 firmware is Espressif's wake-word demo until we replace it.")


def cmd_device_env() -> None:
    header("device-env")
    kv("host.os", platform.platform())
    kv("host.python", sys.version.split()[0])
    kv("host.python_executable", sys.executable)
    kv("host.machine", platform.machine())
    kv("host.processor", platform.processor() or "(n/a)")
    kv("host.esptool", _esptool_version())
    interesting = [
        "IDF_PATH",
        "IDF_TOOLS_PATH",
        "IDF_PYTHON_ENV_PATH",
        "IDF_COMPONENT_MANAGER",
        "IDF_TARGET",
        "ESPPORT",
        "ESPBAUD",
        "ESPCHIP",
        "VIRTUAL_ENV",
        "PORT",
    ]
    print("  host.environment:")
    any_set = False
    for key in interesting:
        if key in os.environ and os.environ[key]:
            kv(key, os.environ[key], indent=2)
            any_set = True
    if not any_set:
        kv("(none of IDF_*/ESP* set)", "unset — expected until firmware builds exist", indent=2)
    try:
        with Bootloader(reset_after=True, flash=False) as bl:
            info = chip_summary(bl.esp)
            sec = security_lines(bl.esp)
            print("  device.platform:")
            kv("chip", info.get("description") or info.get("chip"), indent=2)
            kv("mac", info.get("mac"), indent=2)
            kv("features", ", ".join(info.get("features") or []) or "(unknown)", indent=2)
            kv("crystal_mhz", info.get("crystal_mhz"), indent=2)
            print("  device.security:")
            for k, v in sec.items():
                kv(k, v, indent=2)
    except TaskError as exc:
        warn(str(exc))


def cmd_device_flash() -> None:
    header("device-flash")
    with Bootloader(reset_after=True, flash=True) as bl:
        info = flash_info(bl.esp)
        chip = chip_summary(bl.esp)
        ok(info.get("flash_size") or "flash attached")
        for k, v in info.items():
            kv(k, v)
        psram = [f for f in (chip.get("features") or []) if "PSRAM" in str(f).upper()]
        kv("psram", ", ".join(psram) if psram else "not reported by ROM (BOX-3 is N16R16: 16 MB PSRAM)")
        kv("psram_note", "PSRAM is runtime RAM; ROM download mode cannot dump it.")
        kv("other_host_storage", "Dock USB-A mass-storage is on the device USB host, not this USB-C serial port.")


def cmd_device_partitions() -> None:
    header("device-partitions")
    with Bootloader(reset_after=True, flash=True) as bl:
        parts, md5 = _read_parts(bl)
        flash = flash_info(bl.esp)
        if not parts:
            fail("no partition table at 0x8000")
            kv("hint", "empty flash, encrypted table, or non-standard offset")
            return
        ok(f"{len(parts)} partition(s)")
        kv("table_offset", f"0x{PARTITION_TABLE_OFFSET:x}")
        if md5:
            kv("table_md5", md5)
        kv("flash_size", flash.get("flash_size"))
        mapped = sum(p.size for p in parts)
        kv("mapped_by_table", _fmt_bytes(mapped))
        flash_bytes = flash.get("flash_bytes")
        if isinstance(flash_bytes, int) and flash_bytes >= mapped:
            kv("unallocated", _fmt_bytes(flash_bytes - mapped))
        print("  partitions:")
        for p in parts:
            kv(
                p.name or "(unnamed)",
                f"{p.type_name}/{p.subtype_name}  off=0x{p.offset:x}  size={_fmt_bytes(p.size)}",
                indent=2,
            )


def cmd_device_fs() -> None:
    header("device-fs")
    with Bootloader(reset_after=True, flash=True) as bl:
        parts, _ = _read_parts(bl)
        fs_parts = [p for p in parts if p.is_fs or p.subtype_name in ("nvs", "fat", "spiffs", "littlefs")]
        if not fs_parts:
            warn("no filesystem or NVS partitions in the table")
            kv("on_chip", "SPI flash is still addressable as raw partitions (see device-partitions)")
            return
        ok(f"{len(fs_parts)} filesystem/data volume(s)")
        for p in fs_parts:
            head = bl.read(p.offset, min(p.size, 4096))
            probed = probe_region(head, p.subtype_name)
            if probed.get("kind", "").startswith("FAT"):
                probed = fat_volume_and_files(head, bl.read, p)
            print(f"  {p.name}:")
            kv("type", f"{p.type_name}/{p.subtype_name}", indent=2)
            kv("size", _fmt_bytes(p.size), indent=2)
            for k, v in probed.items():
                if k == "subtype":
                    continue
                kv(k, v if not isinstance(v, list) else (", ".join(v) or "(empty)"), indent=2)


def cmd_device_data() -> None:
    header("device-data")
    with Bootloader(reset_after=True, flash=True) as bl:
        parts, _ = _read_parts(bl)
        data_parts = [p for p in parts if p.type_name == "data"]
        if not data_parts:
            warn("no data-type partitions")
            return
        ok(f"{len(data_parts)} data partition(s)")
        kv(
            "privacy",
            "NVS values are not printed (Wi-Fi / tokens may live there).",
        )
        for p in data_parts:
            sample = min(p.size, 8192)
            head = bl.read(p.offset, sample)
            probed = probe_region(head, p.subtype_name)
            if "FAT" in str(probed.get("kind", "")) or p.subtype_name == "fat":
                probed = fat_volume_and_files(head, bl.read, p)
            populated = not erased(head)
            print(f"  {p.name}:")
            kv("role", p.subtype_name, indent=2)
            kv("size", _fmt_bytes(p.size), indent=2)
            kv("populated", populated, indent=2)
            kv("kind", probed.get("kind"), indent=2)
            if isinstance(probed.get("root_entries"), list):
                kv(
                    "files",
                    ", ".join(probed["root_entries"]) or "(no directory entries)",
                    indent=2,
                )
            if probed.get("volume_label"):
                kv("volume_label", probed["volume_label"], indent=2)
            if probed.get("note"):
                kv("note", probed["note"], indent=2)
            used = 0
            pages = max(1, len(head) // 4096)
            for i in range(pages):
                page = head[i * 4096 : (i + 1) * 4096]
                if page and not erased(page):
                    used += 1
            kv("non_erased_pages_in_sample", f"{used}/{pages} of first {_fmt_bytes(sample)}", indent=2)
        kv(
            "not_visible_from_this_port",
            "Dock USB-A disks/cameras; PSRAM contents; any SD slot on the full kit.",
        )


def _read_parts(bl: Bootloader) -> tuple[list[Partition], str | None]:
    blob = bl.read(PARTITION_TABLE_OFFSET, PARTITION_TABLE_SIZE)
    return parse_partition_table(blob)


def _app_desc_from_parts(bl: Bootloader, parts: list[Partition]) -> dict | None:
    apps = [p for p in parts if p.is_app]
    for p in apps:
        try:
            blob = bl.read(p.offset, min(p.size, 0x4000))
        except TaskError:
            continue
        desc = find_app_desc(blob)
        if desc:
            desc["partition"] = p.name
            return desc
    # Fallback: image at 0x10000 is a common factory offset
    try:
        desc = find_app_desc(bl.read(0x10000, 0x4000))
        if desc:
            desc["partition"] = "0x10000"
            return desc
    except TaskError:
        pass
    return None


def _esptool_version() -> str:
    try:
        import esptool

        return getattr(esptool, "__version__", "installed")
    except ImportError:
        return "not installed"


# ---------------------------------------------------------------------------
# Meta tasks
# ---------------------------------------------------------------------------

SMALL = {
    "device-detect": cmd_device_detect,
    "device-port": cmd_device_port,
    "device-sync": cmd_device_sync,
    "device-chip": cmd_device_chip,
    "device-os": cmd_device_os,
    "device-software": cmd_device_software,
    "device-env": cmd_device_env,
    "device-flash": cmd_device_flash,
    "device-partitions": cmd_device_partitions,
    "device-fs": cmd_device_fs,
    "device-data": cmd_device_data,
}


def cmd_connect() -> None:
    print("== connect  (USB presence → port → ROM sync → chip)")
    print("   plug USB-C into the box, not the dock")
    print()
    cmd_device_detect()
    print()
    cmd_device_port()
    print()
    # One bootloader session covers sync + chip
    header("device-sync")
    with Bootloader(reset_after=False, flash=False) as bl:
        name = getattr(bl.esp, "CHIP_NAME", "Espressif SoC")
        ok(f"ROM bootloader synced on {bl.port} ({name})")
        kv("communication", "SLIP download-mode session established")
        print()
        header("device-chip")
        info = chip_summary(bl.esp)
        ok(str(info.get("description") or info.get("chip")))
        print_chip(info)
        # reset on the way out
        from esptool.cmds import reset_chip

        try:
            reset_chip(bl.esp, "watchdog-reset")
        except Exception:
            try:
                reset_chip(bl.esp, "hard-reset")
            except Exception:
                pass
    print()


def cmd_system() -> None:
    print("== system  (OS, software versions, platform environment)")
    print()
    port = pick_port().port
    desc = None
    boot = None
    chip = {}
    sec: dict = {}
    with Bootloader(reset_after=True, flash=True) as bl:
        parts, _ = _read_parts(bl)
        desc = _app_desc_from_parts(bl, parts)
        try:
            boot = find_bootloader_desc(bl.read(BOOTLOADER_OFFSET, 0x4000))
        except TaskError:
            boot = None
        chip = chip_summary(bl.esp)
        sec = security_lines(bl.esp)
    log = capture_boot_log(port)
    parsed = parse_boot_log(log)

    header("device-os")
    kv("device_os", "ESP-IDF on FreeRTOS (Xtensa LX7)")
    idf = (desc or {}).get("idf") or parsed.get("boot_idf_line") or "(unknown)"
    kv("esp_idf", idf)
    kv("rtos", parsed.get("rtos") or "FreeRTOS (ESP-IDF default)")
    kv("cpu", chip.get("description") or "ESP32-S3")
    kv("host_os", f"{platform.system()} {platform.release()} ({platform.machine()})")
    print()

    header("device-software")
    if desc:
        kv("project", desc.get("project"))
        kv("app_version", desc.get("version"))
        kv("esp_idf", desc.get("idf"))
        kv("built", desc.get("built"))
        kv("partition", desc.get("partition"))
        kv("elf_sha256", desc.get("elf_sha256"))
        ok(f"{desc.get('project') or 'app'} {desc.get('version') or ''}".strip())
    else:
        warn("no app descriptor found")
    if boot:
        kv("bootloader_version", boot.get("version"))
        kv("bootloader_idf", boot.get("idf"))
        kv("bootloader_built", boot.get("built"))
    kv("host_esptool", _esptool_version())
    print()

    header("device-env")
    kv("host.os", platform.platform())
    kv("host.python", f"{sys.version.split()[0]} ({sys.executable})")
    kv("host.esptool", _esptool_version())
    print("  host.environment:")
    shown = False
    for key in (
        "IDF_PATH",
        "IDF_TOOLS_PATH",
        "IDF_PYTHON_ENV_PATH",
        "IDF_TARGET",
        "ESPPORT",
        "ESPBAUD",
        "VIRTUAL_ENV",
        "PORT",
    ):
        if os.environ.get(key):
            kv(key, os.environ[key], indent=2)
            shown = True
    if not shown:
        kv("(IDF_*/ESP* )", "unset", indent=2)
    print("  device.platform:")
    kv("chip", chip.get("description"), indent=2)
    kv("mac", chip.get("mac"), indent=2)
    kv("features", ", ".join(chip.get("features") or []) or "(unknown)", indent=2)
    print("  device.security:")
    for k, v in sec.items():
        kv(k, v, indent=2)
    if parsed.get("boot_idf_line"):
        print("  device.boot_log:")
        kv("idf", parsed.get("boot_idf_line"), indent=2)
        if parsed.get("chip_line"):
            kv("chip", parsed["chip_line"], indent=2)
    print()


def cmd_storage() -> None:
    print("== storage  (flash, partitions, filesystems, user data)")
    print()
    with Bootloader(reset_after=True, flash=True) as bl:
        flash = flash_info(bl.esp)
        chip = chip_summary(bl.esp)
        parts, md5 = _read_parts(bl)

        header("device-flash")
        ok(flash.get("flash_size") or "flash attached")
        for k, v in flash.items():
            kv(k, v)
        psram = [f for f in (chip.get("features") or []) if "PSRAM" in str(f).upper()]
        kv("psram", ", ".join(psram) if psram else "ROM did not list PSRAM (kit is 16 MB octal PSRAM)")
        kv("psram_note", "Not readable in download mode.")
        print()

        header("device-partitions")
        if not parts:
            fail("no partition table at 0x8000")
        else:
            ok(f"{len(parts)} partition(s)")
            kv("table_offset", f"0x{PARTITION_TABLE_OFFSET:x}")
            if md5:
                kv("table_md5", md5)
            mapped = sum(p.size for p in parts)
            kv("mapped_by_table", _fmt_bytes(mapped))
            flash_bytes = flash.get("flash_bytes")
            if isinstance(flash_bytes, int) and flash_bytes >= mapped:
                kv("unallocated", _fmt_bytes(flash_bytes - mapped))
            print("  partitions:")
            for p in parts:
                kv(
                    p.name or "(unnamed)",
                    f"{p.type_name}/{p.subtype_name}  off=0x{p.offset:x}  size={_fmt_bytes(p.size)}",
                    indent=2,
                )
        print()

        header("device-fs")
        fs_parts = [
            p
            for p in parts
            if p.is_fs or p.subtype_name in ("nvs", "fat", "spiffs", "littlefs")
        ]
        if not fs_parts:
            warn("no filesystem/NVS partitions")
        else:
            ok(f"{len(fs_parts)} volume(s)")
            for p in fs_parts:
                head = bl.read(p.offset, min(p.size, 4096))
                probed = probe_region(head, p.subtype_name)
                if "FAT" in str(probed.get("kind", "")) or p.subtype_name == "fat":
                    probed = fat_volume_and_files(head, bl.read, p)
                print(f"  {p.name}:")
                kv("type", f"{p.type_name}/{p.subtype_name}", indent=2)
                kv("size", _fmt_bytes(p.size), indent=2)
                for k, v in probed.items():
                    if k in ("subtype",):
                        continue
                    kv(k, v if not isinstance(v, list) else (", ".join(v) or "(empty)"), indent=2)
        print()

        header("device-data")
        data_parts = [p for p in parts if p.type_name == "data"]
        kv("privacy", "NVS values are not printed.")
        for p in data_parts:
            sample = min(p.size, 8192)
            head = bl.read(p.offset, sample)
            probed = probe_region(head, p.subtype_name)
            if "FAT" in str(probed.get("kind", "")) or p.subtype_name == "fat":
                probed = fat_volume_and_files(head, bl.read, p)
            print(f"  {p.name}:")
            kv("role", p.subtype_name, indent=2)
            kv("size", _fmt_bytes(p.size), indent=2)
            kv("populated", not erased(head), indent=2)
            kv("kind", probed.get("kind"), indent=2)
            files = probed.get("root_entries")
            if isinstance(files, list):
                kv("files", ", ".join(files) or "(none)", indent=2)
        kv(
            "not_visible_from_this_port",
            "Dock USB-A storage/camera; PSRAM; full-kit microSD.",
        )
    print()


def cmd_report() -> None:
    cmd_connect()
    cmd_system()
    cmd_storage()


META = {
    "connect": cmd_connect,
    "system": cmd_system,
    "storage": cmd_storage,
    "report": cmd_report,
}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="device.py",
        description="BOX-3 inspect tasks (see `make help`).",
    )
    parser.add_argument("task", help="Task name (device-* or connect/system/storage/report)")
    args = parser.parse_args(argv)
    name = args.task
    try:
        if name in META:
            META[name]()
            return 0
        if name in SMALL:
            SMALL[name]()
            return 0
        print(f"unknown task: {name}", file=sys.stderr)
        print("known:", ", ".join([*META, *SMALL]), file=sys.stderr)
        return 2
    except TaskError as exc:
        fail(str(exc))
        return 1


if __name__ == "__main__":
    sys.exit(main())
