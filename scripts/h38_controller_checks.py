#!/usr/bin/env python3
"""Host-only failure injection for H38 controller gates, clocks, and recovery."""
from __future__ import annotations

import tempfile
import base64
import hashlib
import json
import shutil
import struct
from pathlib import Path
from unittest.mock import patch

import h38_attached_filesystem as h38


def synthetic_success_capture(intent, elf):
    c = h38.contract
    epoch = intent["epoch"]

    def rec(event, fields):
        values = {"epoch": epoch, "elf_sha256": elf, **fields}
        return ("H38,1," + event + "," + ",".join(f"{k}={v}" for k, v in values.items()) + "\n").encode()

    bpb = bytearray(512)
    bpb[:3] = b"\xeb\x58\x90"
    bpb[3:11] = b"MSDOS5.0"
    struct.pack_into("<H", bpb, 11, 512)
    bpb[13] = 8
    struct.pack_into("<H", bpb, 14, 32)
    bpb[16] = 2
    struct.pack_into("<I", bpb, 32, 1_048_576)
    struct.pack_into("<I", bpb, 36, 1025)
    struct.pack_into("<I", bpb, 44, 2)
    struct.pack_into("<H", bpb, 48, 1)
    struct.pack_into("<H", bpb, 50, 6)
    bpb[510:] = b"\x55\xaa"
    bpb = bytes(bpb)
    mbr = c.build_mbr(epoch)
    mh = lambda data: hashlib.sha256(data).hexdigest()
    rows = []
    add = lambda event, fields: rows.append(rec(event, fields))
    add("BOOT", {"reset_reason": "1"})
    add("TRANSPORT", dict(c.TRANSPORT))
    for event in ("HOST", "SLOT", "CARD"):
        add(event, {"error": "0"})
    add("GEOMETRY", {"sectors": "121503744", "sector_bytes": "512", "capacity_bytes": "62209916928",
                      "bus_width": "4", "real_freq_khz": "20000", "ddr": "0"})
    add("CID_PRIVATE", {"mfg_id": "1", "oem_id": "1", "revision": "1", "serial": "1", "date": "1",
                         "name_size": "0", "name_hex": ""})
    add("READY", {"accepts": "BIND"})
    add("IDENTITY_MATCH", {"reference_epoch": c.H35_REFERENCE_EPOCH, "match": "1", "error": "0"})
    add("LAYOUT_RESULT", {"physical_lba": "0", "write_sectors": "1", "write_bytes": "512", "write_count": "1",
                           "readback_match": "1", "readback_sha256": mh(mbr),
                           "readback_base64_private": base64.b64encode(mbr).decode(), "trim_requests": "0",
                           "erase_calls": "0", "status": "ok", "error": "0"})
    add("FORMAT_START", {"volume_sectors": "1048576", "sector_bytes": "512", "fat_type": "FAT32",
                          "fat_count": "2", "allocation_unit_bytes": "4096", "format_flags": "10",
                          "work_buffer_bytes": "4096", "write_limit_bytes": "4194304",
                          "read_limit_bytes": "16777216", "time_limit_ms": "120000"})
    add("FORMAT_RESULT", {"f_result": "0", "volume_sectors": "1048576", "sector_bytes": "512", "fat_type": "FAT32",
                           "fat_count": "2", "allocation_unit_bytes": "4096", "cluster_count": "130811",
                           "fat_sectors": "1025", "root_cluster_sectors": "8", "read_bytes": "4096",
                           "write_bytes": "1055744", "write_calls": "263", "max_call_sectors": "8",
                           "trim_requests": "1", "erase_calls": "0", "bpb_valid": "1", "bpb_sha256": mh(bpb),
                           "bpb_base64_private": base64.b64encode(bpb).decode(), "elapsed_us": "1",
                           "status": "ok", "error": "0"})
    mount = {"kind": "initial", "cycle": "0", "mounted": "1", "fs_type": "3", "sector_bytes": "512",
             "volume_sectors": "1048576", "allocation_unit_bytes": "4096", "cluster_count": "130811",
             "total_bytes": "535801856", "free_bytes": "499998720", "read_bytes": "512", "write_bytes": "0",
             "elapsed_us": "1", "status": "ok", "error": "0"}
    add("MOUNT_RESULT", mount)
    retained = (0, 10, 19, 20, 30, 39)
    sizes = {i: 65536 if i < 20 else 196608 for i in range(40)}
    seeds = {i: 1048576 + sizes[i] + (i if i < 20 else i - 20) for i in range(40)}
    digests = {i: c._pattern_sha256(sizes[i], seeds[i]) for i in range(40)}
    for i in range(40):
        size = sizes[i]
        add("IO_RESULT", {"index": str(i), "size_bytes": str(size), "seed": str(seeds[i]),
                           "expected_sha256": digests[i], "actual_sha256": digests[i],
                           "payload_write_bytes": str(size), "payload_read_bytes": str(size),
                           "total_bytes": str(size * 2), "free_before": "499998720", "free_after": "499990528",
                           "fflush_ok": "1", "fsync_ok": "1", "close_ok": "1", "rename_rc": "0",
                           "rename_errno": "0", "checksum_match": "1", "retained": "1" if i in retained else "0",
                           "deleted": "0" if i in retained else "1", "failure_op": "none", "open_us": "1",
                           "write_us": "1", "fflush_us": "1", "fsync_us": "1", "close_us": "1",
                           "rename_us": "1", "read_verify_us": "1", "delete_us": "1", "status": "ok", "error": "0"})
    for cycle in range(5):
        remount = dict(mount, kind="remount", cycle=str(cycle))
        add("MOUNT_RESULT", remount)
        add("PROBE_RESULT", {"cycle": str(cycle), "size_bytes": "65536", "seed": "849697315",
                             "verified": "1", "elapsed_us": "1", "status": "ok", "error": "0"})
        for i in retained:
            add("RETAINED_RESULT", {"cycle": str(cycle), "index": str(i), "size_bytes": str(sizes[i]),
                                    "seed": str(seeds[i]), "expected_sha256": digests[i],
                                    "actual_sha256": digests[i], "checksum_match": "1", "status": "ok", "error": "0"})
    add("DIRECTORY_RESULT", {"entries": "11", "owned_parts": "1", "unknown_entries": "0", "cleanup_count": "1",
                              "status": "ok", "error": "0"})
    add("RENAME_RESULT", {"old_target_valid": "1", "source_present": "1", "rename_rc": "-1", "rename_errno": "17",
                          "expected_errno": "17", "old_target_preserved": "1",
                          "target_sha256": c._pattern_sha256(16384, 0x610001),
                          "source_sha256": c._pattern_sha256(24576, 0x610002), "status": "ok", "error": "0"})
    add("RECLAIM_RESULT", {"bytes_before": "499998720", "bytes_after": "500002816", "reclaimed_bytes": "4096",
                           "expected_bytes": "4096", "residual_owned_files": "10", "status": "ok", "error": "0"})
    cleanup = {"sd_unmount_attempted": "1", "sd_unmount_error": "0", "host_deinit_attempted": "1",
               "host_deinit_error": "0", "power_off_attempted": "1", "power_off_error": "0"}
    add("CLEANUP", cleanup)
    add("COMPLETE", {"result": "io_complete", "failure_stage": "none", "error": "0", "command_count": "4",
                      "read_bytes": "20000256", "write_bytes": str(512 + 1055744 + 5353984),
                      "mbr_write_count": "1", "format_write_bytes": "1055744", "io_write_bytes": "5353984",
                      "trim_requests": "1", "erase_calls": "0", "out_of_bounds_attempts": "0", "io_file_count": "40",
                      "retained_file_bytes": "893440", "probe_count": "5", "mount_count": "6", "remount_count": "5",
                      "retained_check_count": "30", "reclaim_status": "ok", **cleanup,
                      "bound": "1", "scope": "bounded_fat32_filesystem_io", "media_writes": "2000"})
    return rows, b"".join(rows)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def clock_checks():
    tick = [0.0]
    clock = h38.RunClocks(lambda: tick[0])
    clock.start_capture()
    clock.start_session()
    clock.start_phase("FORMAT")
    tick[0] = 119.9
    clock.end_phase("FORMAT")
    clock.start_phase("IO")
    tick[0] = 1020.0
    try:
        clock.check()
    except h38.DeadlineError as exc:
        check(exc.name == "io_phase", "wrong phase deadline")
    else:
        raise AssertionError("IO phase deadline was not enforced")
    tick[0] = 2699.999
    clock.phase_start = None
    clock.phase_name = None
    clock.capture_start = None
    clock.session_start = None
    try:
        clock.require_new_work(2)
    except h38.DeadlineError as exc:
        check(exc.name == "restore_reserve", "recovery reserve was not preserved")
    else:
        raise AssertionError("new mutation entered recovery reserve")


def clock_freeze_checks():
    tick = [0.0]
    clock = h38.RunClocks(lambda: tick[0])
    tick[0] = 2.0
    clock.start_capture()
    clock.start_session()
    tick[0] = 3.0
    clock.start_phase("FORMAT")
    tick[0] = 3.25
    clock.end_phase("FORMAT")
    tick[0] = 4.0
    terminal_received = tick[0]
    # Parser validation completes after receipt; freeze elapsed at receipt time.
    tick[0] = 4.5
    clock.freeze_capture("validated_terminal", terminal_received)
    frozen = clock.summary()
    tick[0] = 3600.0  # Recovery duration belongs to master only.
    after_restore = clock.summary()
    check(frozen["capture_elapsed_ms"] == 2000 and frozen["session_elapsed_ms"] == 2000,
          "terminal capture/session elapsed did not end at terminal receipt")
    check(after_restore["capture_elapsed_ms"] == frozen["capture_elapsed_ms"] and
          after_restore["session_elapsed_ms"] == frozen["session_elapsed_ms"],
          "restore time leaked into frozen capture/session elapsed")
    check(after_restore["master_elapsed_ms"] > frozen["master_elapsed_ms"],
          "restore time did not remain in master elapsed")
    try:
        tick[0] = 2000.0  # Beyond capture/session caps, still before recovery reserve.
        clock.check()
    except h38.DeadlineError as exc:
        raise AssertionError("frozen capture/session incorrectly hit their limits after terminal") from exc
    fixed_end = clock.capture_end

    def unexpected_wait(_):
        raise AssertionError("post-terminal error waited for an already completed capture")

    post_terminal_error = OSError("injected context-close failure")
    try:
        raise post_terminal_error
    except OSError as caught:
        deadline = h38._wait_for_capture_abort(clock, unexpected_wait)
        preserved_failure = deadline if deadline is not None else caught
    check(deadline is None and preserved_failure is post_terminal_error and clock.capture_end == fixed_end,
          "post-terminal error changed frozen capture state or entered active wait")
    phases = after_restore["phases"]
    check(len(phases) == 1 and phases[0]["name"] == "FORMAT" and
          phases[0]["elapsed_ms"] == 250 and phases[0]["end_reason"] == "milestone",
          "completed phase timing endpoints were not retained")

    timeout_tick = [0.0]
    timeout_limits = dict(h38.contract.CAPTURE_LIMITS)
    timeout_limits["capture_wall_ms"] = 100_000
    timeout_limits["session_ms"] = 200_000
    timeout = h38.RunClocks(lambda: timeout_tick[0], timeout_limits)
    timeout.start_capture()
    timeout.start_session()
    timeout_tick[0] = 10.0
    timeout.start_phase("IO")
    timeout_tick[0] = 101.0
    deadline = h38._wait_for_capture_abort(timeout, lambda _: None)
    check(isinstance(deadline, h38.DeadlineError) and deadline.name == "capture_wall",
          "capture timeout fixture did not hit its configured authoritative deadline")
    at_abort = timeout.summary()
    timeout_tick[0] = 3000.0
    during_recovery = timeout.summary()
    check(at_abort["capture_elapsed_ms"] == 101000 and at_abort["session_elapsed_ms"] == 101000,
          "timeout capture did not freeze at the abort boundary")
    check(during_recovery["capture_elapsed_ms"] == at_abort["capture_elapsed_ms"] and
          during_recovery["session_elapsed_ms"] == at_abort["session_elapsed_ms"] and
          during_recovery["master_elapsed_ms"] > at_abort["master_elapsed_ms"],
          "timeout restore time changed capture/session instead of master only")
    timeout_phase = during_recovery["phases"]
    check(len(timeout_phase) == 1 and timeout_phase[0]["name"] == "IO" and
          timeout_phase[0]["elapsed_ms"] == 91000 and
          timeout_phase[0]["end_reason"] == "deadline:capture_wall",
          "active phase timing was not frozen with the timeout capture")


def short_write_check():
    class Port:
        def write(self, data):
            return len(data) - 1

        def flush(self):
            raise AssertionError("short write must not flush or start phase")

    clock = h38.RunClocks(lambda: 0.0)
    clock.start_capture()
    try:
        h38._send_command(Port(), b"H38C,1,FORMAT,x\n", clock, 120000)
    except h38.TransportError as exc:
        check("short" in str(exc), "wrong short-write error")
    else:
        raise AssertionError("short phase write was accepted")
    check(clock.phase_start is None, "phase timer started after short write")

    class FullWritePort:
        def __init__(self):
            self.calls = []

        def write(self, data):
            self.calls.append(data)
            return len(data)

        def flush(self):
            raise AssertionError("serial flush can block outside pyserial write timeout")

    full_clock = h38.RunClocks(lambda: 0.0)
    full_clock.start_capture()
    full = FullWritePort()
    h38._send_command(full, b"H38C,1,FORMAT,x\n", full_clock, 120000)
    check(len(full.calls) == 1, "complete command was not sent exactly once")
    check(full_clock.phase_name == "FORMAT", "FORMAT timer did not start after bounded write")


def capture_failure_checks():
    class Port:
        dtr = False
        rts = False
        port = None

        def __init__(self):
            self.writes = []

        def __enter__(self):
            return self

        def __exit__(self, *_):
            return False

        def reset_input_buffer(self):
            pass

        def write(self, data):
            self.writes.append(data)
            return len(data)

    port = Port()

    class SerialModule:
        @staticmethod
        def Serial(**_):
            return port

    intent = {"epoch": "a" * 32}
    tick = [0.0]
    clocks = h38.RunClocks(lambda: tick[0])

    def malformed_then_timeout(*_args):
        yield h38.contract.Record("MALFORMED", {})
        raise h38.DeadlineError("capture_wall")

    patches = (
        patch.object(h38.contract, "bind_command", return_value=b"H38C,1,BIND,x\n"),
        patch.object(h38.contract, "phase_command", side_effect=lambda name, *_: f"H38C,1,{name},x\n".encode()),
        patch.object(h38.contract, "validate_command_sequence"),
        patch.object(h38, "_record_stream", side_effect=malformed_then_timeout),
    )
    for item in patches:
        item.__enter__()
    try:
        with tempfile.TemporaryDirectory(prefix="h38-capture-failure-") as folder:
            try:
                h38._capture_h38("fake", Path(folder), {"runtime_elf_sha256": "b" * 64},
                                 intent, SerialModule, clocks)
            except h38.DeadlineError as exc:
                check(exc.name == "capture_wall", "mismatch did not wait for authoritative timeout")
            else:
                raise AssertionError("malformed evidence without terminal was accepted")
        check(port.writes == [], "mismatch dispatched a later command")
    finally:
        for item in reversed(patches):
            item.__exit__(None, None, None)

    limits = dict(h38.contract.CAPTURE_LIMITS)
    limits.update(capture_wall_ms=10, session_ms=10)
    tick = [0.0]
    clocks = h38.RunClocks(lambda: tick[0], limits)
    clocks.start_capture()
    errors = []

    class BrokenReader:
        def read(self, _):
            tick[0] += .02
            raise OSError("injected read failure")

    with tempfile.TemporaryFile() as file:
        try:
            next(h38._record_stream(BrokenReader(), file, bytearray(), clocks, bytearray(), errors))
        except h38.DeadlineError as exc:
            check(exc.name == "capture_wall", "read failure fabricated a transport timeout")
        else:
            raise AssertionError("no-terminal stream did not reach authoritative deadline")
    check(errors and "serial read failed" in errors[0], "read failure was not latched")

    check(h38._safe_terminal(b"H38,1,COMPLETE,epoch=" + b"c" * 32,
                             {"epoch": "a" * 32}, "b" * 64) is None,
          "unbound terminal was accepted as safe")


def boot_record_checks():
    expected = {"project": "family_link_demo", "version": "1.0", "idf": "v5.4.2",
                "elf_sha256": "a" * 64}
    startup = (b"I (100) boot: Loaded app from partition at offset 0x10000\n"
               b"I (200) app_start: Project name: family_link_demo\n"
               b"I (201) app_start: App version: 1.0\n"
               b"I (202) app_start: ELF file SHA256: aaaaaaaa\n"
               b"I (203) app_start: IDF version: v5.4.2\n"
               b"I (300) main_task: Calling app_main()\n")

    class Port:
        dtr = False
        rts = False
        port = None

        def __init__(self, raw):
            self.done = False
            self.raw = raw

        def __enter__(self):
            return self

        def __exit__(self, *_):
            return False

        def reset_input_buffer(self):
            pass

        def read(self, _):
            if self.done:
                return b""
            self.done = True
            return self.raw

    class SerialModule:
        def __init__(self, raw):
            self.raw = raw

        def Serial(self, **_):
            return Port(self.raw)

    def run_boot(raw):
        with tempfile.TemporaryDirectory(prefix="h38-boot-log-") as folder:
            return h38._boot_original("fake", Path(folder), SerialModule(raw), {"apps": [
                {"name": "factory", "descriptor": expected}]})

    result = run_boot(b"\x1b[32mrst:0x15 (USB_UART_CHIP_RESET)\x1b[0m\n" + startup)
    check(result["factory_offset_match"] and result["startup_order_match"],
          "factory startup lines were not bound to the loaded partition")
    check(result["usb_reset_code"] == "21" and result["usb_reset_namespace"] == "rom",
          "ROM USB reset code was confused with runtime ESP_RST_USB")

    pinned_startup = startup.replace(b"IDF version:", b"ESP-IDF:").replace(b"\n", b"\r\n")
    check(run_boot(pinned_startup)["sdk_match"], "pinned ESP-IDF log label rejected")
    try:
        run_boot(pinned_startup.replace(b"ESP-IDF: v5.4.2", b"ESP-IDF: v5.4.1"))
    except ValueError:
        pass
    else:
        raise AssertionError("wrong pinned SDK value accepted")

    # ROM RTC reset reasons are actual watchdog events even if the named tag is absent.
    for code in (7, 8, 9, 11, 13, 16, 17, 18):
        try:
            run_boot(f"rst:0x{code:x}\n".encode("ascii") + startup)
        except ValueError:
            pass
        else:
            raise AssertionError(f"ROM watchdog reset code {code} was accepted")

    # The anchored event parser ignores legend/diagnostic mentions, and runtime
    # ESP_RST_USB=11 is not the ROM reset code 11 watchdog event.
    legend = (b"Reset legend: rst:0x07 (TG0WDT_SYS_RESET), USB_UART_CHIP_RESET\n"
              b"I (50) app: observed ESP_RST_USB=11\n")
    legend_result = run_boot(legend + startup)
    check(legend_result["watchdog_seen"] is False and legend_result["usb_reset_code"] == "none" and
          legend_result["usb_reset_namespace"] == "none",
          "legend/runtime USB text was misread as a ROM reset event")

    for tag in ("TG0WDT_SYS_RESET", "TG0WDT_SYS_RST", "TGWDT_CPU_RST"):
        try:
            run_boot(f"rst:0x1f ({tag})\n".encode("ascii") + startup)
        except ValueError:
            pass
        else:
            raise AssertionError("actual ROM watchdog tag with unknown code was accepted")


def binding_and_review_checks():
    manifest = {"runtime_elf_sha256": "a" * 64,
                "artifacts": {"family_link_demo.elf": {"sha256": "a" * 64}}}
    h38._validate_runtime_elf_binding(manifest, {"elf_sha256": "a" * 64})
    try:
        h38._validate_runtime_elf_binding(manifest, {"elf_sha256": "b" * 64})
    except ValueError:
        pass
    else:
        raise AssertionError("runtime ELF descriptor mismatch was accepted")
    manifest["artifacts"]["family_link_demo.elf"]["sha256"] = "c" * 64
    try:
        h38._validate_runtime_elf_binding(manifest, {"elf_sha256": "a" * 64})
    except ValueError:
        pass
    else:
        raise AssertionError("ELF artifact hash mismatch was accepted")

    with tempfile.TemporaryDirectory(prefix="h38-current-review-") as folder:
        root = Path(folder)
        request = {"schema": 1, "status": "pending", "epoch": "a" * 32,
                   "intent_sha256": "b" * 64, "manifest_sha256": "c" * 64,
                   "proof_sha256": "d" * 64, "full_sha256": "e" * 64,
                   "partition_sha256": "f" * 64, "nvs_sha256": "1" * 64,
                   "device_fingerprint_sha256": "2" * 64, "reviewer": None}
        path = root / "current-backup-review-private.json"
        path.write_text(json.dumps({**request, "status": "approved", "reviewer": "independent"}))
        clocks = h38.RunClocks(lambda: 0.0)
        check(h38._wait_current_backup_review(root, request, clocks)["reviewer"] == "independent",
              "valid fresh-backup review was not accepted")
        path.write_text(json.dumps({**request, "status": "approved", "full_sha256": "9" * 64,
                                   "reviewer": "independent"}))
        try:
            h38._wait_current_backup_review(root, request, clocks)
        except ValueError:
            pass
        else:
            raise AssertionError("review bound to another backup was accepted")

        short = dict(h38.contract.CAPTURE_LIMITS)
        short.update(flash_restore_ms=1000, restore_reserve_ms=500)
        exhausted = h38.RunClocks(lambda: 0.0, short)
        try:
            h38._wait_current_backup_review(root, request, exhausted)
        except h38.DeadlineError as exc:
            check(exc.name == "restore_reserve", "review wait did not preserve recovery reserve")
        else:
            raise AssertionError("review wait entered the recovery reserve")


def recovery_readback_checks():
    with tempfile.TemporaryDirectory(prefix="h38-recovery-proof-") as folder:
        path = Path(folder) / "readback.bin"
        path.write_bytes(b"verified image")
        good = hashlib.sha256(b"verified image").hexdigest()
        bad = hashlib.sha256(b"different image").hexdigest()
        check(h38._recovery_artifact_matches(path, len(b"verified image"), good),
              "exact recovery readback was rejected")
        check(not h38._recovery_artifact_matches(path, len(b"verified image") + 1, good),
              "short recovery readback was accepted")
        check(not h38._recovery_artifact_matches(path, len(b"verified image"), bad),
              "mismatched recovery readback hash was accepted")
        check(not h38._recovery_artifact_matches(Path(folder) / "missing.bin", 1, good),
              "missing recovery readback was accepted")

    with patch.object(h38.h32, "esptool", return_value=type("NoExit", (), {})()):
        try:
            h38._verify_recovery_device("fake", {"device": {"fingerprint_sha256": "a" * 64}})
        except ValueError:
            pass
        else:
            raise AssertionError("recovery identity without checked process exits was accepted")


def restore_current_checks():
    import subprocess

    full_image = bytes(range(256)) * 4
    pt_image = full_image[128:192]
    nvs_image = full_image[192:224]
    app_image = full_image[256:384]
    descriptor = {"project": "family_link_demo", "version": "1.0", "idf": "v5.4.2",
                  "elf_sha256": "b" * 64}
    current = {"full_sha256": hashlib.sha256(full_image).hexdigest(),
               "partition_sha256": hashlib.sha256(pt_image).hexdigest(),
               "nvs_sha256": hashlib.sha256(nvs_image).hexdigest(),
               "device_fingerprint_sha256": "a" * 64,
               "apps": [{"name": "factory", "offset": 256,
                         "sha256": hashlib.sha256(app_image).hexdigest(), "descriptor": descriptor}]}
    baseline = {"device": {"fingerprint_sha256": "a" * 64},
                "nvs": {"offset": 192, "bytes": 32},
                "apps": [{"name": "factory", "offset": 256, "size": 128}],
                "partition_table": {"entries": []}}
    boot_ok = {"project_match": True, "version_match": True, "sdk_match": True,
               "elf_prefix_match": True, "factory_offset_match": True,
               "startup_order_match": True, "app_main_seen": True,
               "panic_seen": False, "watchdog_seen": False,
               "usb_reset_code": "21", "usb_reset_namespace": "rom"}

    def run_case(mode):
        identity_count = [0]
        full_reads = [0]
        reads = []
        image = Path(tempfile.mkdtemp(prefix="h38-restore-case-"))
        full_path = image / "current-full.bin"
        full_path.write_bytes(full_image)

        def fake_esptool(_port, args, _timeout, capture=False):
            check(capture is True, "restore subprocess output was not kept private")
            if "write_flash" in args:
                if mode == "missing_write_exit":
                    return None
                if mode == "nonzero_write":
                    raise subprocess.CalledProcessError(7, args)
                return subprocess.CompletedProcess(args, 0, "", "")
            if "read_flash" in args:
                offset, count, target = int(args[-3], 0), int(args[-2], 0), Path(args[-1])
                reads.append((offset, count))
                payload = full_image[offset:offset + count]
                if offset == 0 and count == len(full_image):
                    full_reads[0] += 1
                    if mode == "full_first_mismatch" and full_reads[0] == 1:
                        payload = b"X" * count
                    elif mode == "full_persistent_mismatch":
                        payload = b"X" * count
                if mode == "partition_mismatch" and offset == 128:
                    payload = b"P" * count
                if mode == "nvs_mismatch" and offset == 192:
                    payload = b"N" * count
                target.write_bytes(payload)
                return subprocess.CompletedProcess(args, 0, "", "")
            raise AssertionError("unexpected recovery helper command")

        def identity(*_):
            identity_count[0] += 1
            fingerprint = "c" * 64 if mode == "device_mismatch" and identity_count[0] == 2 else "a" * 64
            return {"device": {"fingerprint_sha256": fingerprint}, "processes": [
                {"operation": "recovery_chip_id", "returncode": 0},
                {"operation": "recovery_flash_id", "returncode": 0}]}

        def boot(*_):
            if mode == "boot_mismatch":
                return {**boot_ok, "project_match": False}
            return dict(boot_ok)

        patches = (
            patch.object(h38.h32, "FLASH", len(full_image)),
            patch.object(h38.h32, "PT_OFF", 128),
            patch.object(h38.h32, "PT_SIZE", len(pt_image)),
            patch.object(h38.h32, "esptool", side_effect=fake_esptool),
            patch.object(h38.h32, "partitions", return_value=[]),
            patch.object(h38.h32, "app_desc", return_value=descriptor if mode != "descriptor_mismatch" else {**descriptor, "version": "wrong"}),
            patch.object(h38, "_verify_recovery_device", side_effect=identity),
            patch.object(h38, "_boot_original", side_effect=boot),
        )
        for item in patches:
            item.__enter__()
        try:
            result = h38._restore_current("fake", image, full_path, current, baseline,
                                          object(), h38.RunClocks(lambda: 0.0))
            proof_path = image / "restore-proof-private.json"
            proof = json.loads(proof_path.read_text()) if proof_path.exists() else None
            return result, proof, reads, full_reads[0]
        except ValueError:
            proof_path = image / "restore-proof-private.json"
            proof = json.loads(proof_path.read_text()) if proof_path.exists() else None
            return None, proof, reads, full_reads[0]
        finally:
            for item in reversed(patches):
                item.__exit__(None, None, None)
            shutil.rmtree(image, ignore_errors=True)

    result, proof, reads, full_reads = run_case("success")
    check(result is not None and proof["status"] == "verified", "valid full restore was rejected")
    check(len(reads) == 3 and full_reads == 1, "restore did not independently read full/PT/NVS")

    for mode, expected_code in (("missing_write_exit", None), ("nonzero_write", 7)):
        result, proof, reads, _ = run_case(mode)
        check(result is None and proof and proof["status"] == "failed",
              "restore with missing/nonzero write exit code was accepted")
        check(len(reads) >= 3, "write exit failure stopped independent restore readbacks")
        write_proofs = [x for x in proof["process_exit_codes"] if x["operation"] == "write_flash"]
        check(write_proofs and write_proofs[0]["returncode"] == expected_code,
              "restore did not preserve the actual write subprocess exit code")

    result, proof, reads, full_reads = run_case("full_first_mismatch")
    check(result is None and proof and proof["status"] == "failed" and
          proof["initial_full_image_readback_match"] is False,
          "initial image mismatch followed by retry was incorrectly qualified")
    check(full_reads == 2, "initial full image mismatch did not trigger one best-effort rewrite")

    for mode in ("full_persistent_mismatch", "partition_mismatch", "nvs_mismatch",
                 "descriptor_mismatch", "device_mismatch", "boot_mismatch"):
        result, proof, _reads, _ = run_case(mode)
        check(result is None, f"restore mismatch {mode} was accepted")
        if mode in ("device_mismatch", "boot_mismatch"):
            check(proof is not None and proof["status"] == "failed",
                  f"restore mismatch {mode} was not recorded")


def successful_dispatch_check(crlf=False):
    import h38_sd_contract_checks as fixtures

    intent = fixtures.intent()
    elf = "d" * 64
    rows, raw = synthetic_success_capture(intent, elf)
    if crlf:
        rows = [line.replace(b"\n", b"\r\n") for line in rows]
        raw = b"".join(rows)
    check(h38.contract.parse_capture(raw, intent["epoch"], elf, intent).result == "io_complete",
          "controller success fixture failed the real H38 parser")

    class Port:
        dtr = False
        rts = False
        port = None

        def __init__(self):
            self.writes = []
            self.chunks = iter(rows)

        def __enter__(self):
            return self

        def __exit__(self, *_):
            return False

        def reset_input_buffer(self):
            pass

        def read(self, _):
            tick[0] += .001
            return next(self.chunks, b"")

        def write(self, data):
            self.writes.append(data)
            return len(data)

    port = Port()

    class SerialModule:
        @staticmethod
        def Serial(**_):
            return port

    tick = [0.0]
    clocks = h38.RunClocks(lambda: tick[0])

    with tempfile.TemporaryDirectory(prefix="h38-success-dispatch-") as folder:
        result = h38._capture_h38("fake", Path(folder), {"runtime_elf_sha256": elf},
                                  intent, SerialModule, clocks)
        ledger = [json.loads(line) for line in (Path(folder) / "dispatch-ledger-private.jsonl").read_text().splitlines()]
    sent = [command.split(b",")[2].decode("ascii") for command in port.writes]
    check(sent == ["BIND", "LAYOUT", "FORMAT", "IO", "FINISH"],
          "controller dispatch order differs from H38 stop-and-wait protocol")
    check([entry["command"] for entry in ledger] == sent, "dispatch ledger does not match actual writes")
    check(result["result"] == "io_complete" and result["dispatch_count"] == 5,
          "controller did not accept the synthetic parser-validated success")
    check(clocks.phase_name is None, "FORMAT/IO phase clocks remained active after terminal")
    phase_history = clocks.summary()["phases"]
    check([phase["name"] for phase in phase_history] == ["FORMAT", "IO"] and
          all(phase["end_reason"] == "milestone" for phase in phase_history),
          "successful capture did not preserve both phase timing endpoints")


def recovery_checks():
    with tempfile.TemporaryDirectory(prefix="h38-controller-") as folder:
        root = Path(folder)
        run = root / "run"
        run.mkdir(mode=0o700)
        build = run / "build"
        build.mkdir()
        (build / "manifest-private.json").write_text("{}")
        (build / "family_link_demo.bin").write_bytes(b"app")
        current = root / "current.bin"
        current.write_bytes(b"current")
        meta = {"baseline_full_sha256": "a" * 64}
        intent = {"epoch": "0" * 32}
        baseline = {"_directory": str(root)}
        calls = []
        tick = [0.0]

        def backup(_):
            calls.append("backup")
            return dict(baseline)

        def fresh(*_):
            calls.append("fresh")
            return current, {"full_sha256": "b" * 64}

        def restore(*_):
            calls.append("restore")
            tick[0] = 5401.0
            return {"status": "verified"}

        def approve(*args):
            run_dir = args[0]
            (run_dir / "current-backup-review-private.json").write_text("{}")
            return {"reviewer": "injected"}

        def esptool(_, args, *__, **___):
            calls.append("esptool:" + args[-2])
            if "write_flash" in args:
                raise RuntimeError("injected flash failure")

        common = (
            patch.object(h38, "private_dir", side_effect=lambda p: p),
            patch.object(h38, "validate", return_value={"runtime_elf_sha256": "c" * 64}),
            patch.object(h38, "_review_gate"),
            patch.object(h38, "_linked_review_gate"),
            patch.object(h38, "capture_preflight", return_value={"status": "ready"}),
            patch.object(h38, "_backup", side_effect=backup),
            patch.object(h38, "require_run", return_value=(meta, intent)),
            patch.object(h38, "_fresh_current_backup", side_effect=fresh),
            patch.object(h38, "_current_backup_review_request", side_effect=lambda rd, *_: (
                ((rd / "current-backup-review-request-private.json").write_text("{}"), {"reviewer": None})[1])),
            patch.object(h38, "_wait_current_backup_review", side_effect=approve),
            patch.object(h38, "_restore_current", side_effect=restore),
            patch.object(h38.h32, "esptool", side_effect=esptool),
        )
        exits = [p.__enter__() for p in common]
        try:
            try:
                h38.run_epoch("fake-port", run, root, now=lambda: tick[0],
                              serial_importer=lambda _: object())
            except RuntimeError as exc:
                check("injected" in str(exc), "flash failure was hidden")
            else:
                raise AssertionError("injected flash failure was accepted")
            check("restore" in calls, "mandatory restore was skipped after flash failure")
            check((run / "run-result-private.json").is_file(), "private failure ledger missing")
            check((run / "run-result-private.json").read_text().find("failed_or_incomplete") >= 0,
                  "failed run was marked successful")
            ledger = json.loads((run / "run-result-private.json").read_text())
            check(ledger["clocks"]["master_overrun_ms"] > 0,
                  "late recovery did not record master-clock overrun")
        finally:
            for p in reversed(common):
                p.__exit__(None, None, None)

        # A timed-out synchronous operation may leave no firmware terminal.
        # The controller must use only loader reset, then restore the BOX.
        timed = root / "timed"
        timed.mkdir(mode=0o700)
        (timed / "build").mkdir()
        (timed / "build/manifest-private.json").write_text("{}")
        (timed / "build/family_link_demo.bin").write_bytes(b"app")
        actions = []

        def timed_esptool(_, args, *__, **___):
            actions.append(args[-1])
            if "write_flash" in args:
                raise h38.DeadlineError("capture_wall")

        def timed_restore(*_):
            actions.append("restore")
            return {"status": "verified"}

        def timed_approve(*args):
            (args[0] / "current-backup-review-private.json").write_text("{}")
            return {"reviewer": "injected"}

        timed_patches = (
            patch.object(h38, "private_dir", side_effect=lambda p: p),
            patch.object(h38, "validate", return_value={"runtime_elf_sha256": "c" * 64}),
            patch.object(h38, "_review_gate"), patch.object(h38, "_linked_review_gate"),
            patch.object(h38, "capture_preflight", return_value={"status": "ready"}),
            patch.object(h38, "_backup", return_value=dict(baseline)),
            patch.object(h38, "require_run", return_value=(meta, intent)),
            patch.object(h38, "_fresh_current_backup", return_value=(current, {"full_sha256": "b" * 64})),
            patch.object(h38, "_current_backup_review_request", side_effect=lambda rd, *_: (
                ((rd / "current-backup-review-request-private.json").write_text("{}"), {"reviewer": None})[1])),
            patch.object(h38, "_wait_current_backup_review", side_effect=timed_approve),
            patch.object(h38, "_restore_current", side_effect=timed_restore),
            patch.object(h38.h32, "esptool", side_effect=timed_esptool),
        )
        for p in timed_patches:
            p.__enter__()
        try:
            try:
                h38.run_epoch("fake-port", timed, root, serial_importer=lambda _: object())
            except h38.DeadlineError:
                pass
            else:
                raise AssertionError("injected timeout was accepted")
            check("chip_id" in actions, "emergency loader reset omitted")
            check(actions.index("chip_id") < actions.index("restore"),
                  "BOX restore began before timeout loader reset")
            check("restore" in actions, "BOX restore omitted after timeout")
        finally:
            for p in reversed(timed_patches):
                p.__exit__(None, None, None)

        early = root / "early-backup-failure"
        early.mkdir(mode=0o700)
        (early / "build").mkdir()
        (early / "build/manifest-private.json").write_text("{}")
        (early / "build/family_link_demo.bin").write_bytes(b"app")
        early_actions = []

        def reset_process(_, args, *__, **___):
            early_actions.append(args)
            return type("Completed", (), {"returncode": 0})()

        early_patches = (
            patch.object(h38, "private_dir", side_effect=lambda p: p),
            patch.object(h38, "validate", return_value={"runtime_elf_sha256": "c" * 64}),
            patch.object(h38, "_review_gate"), patch.object(h38, "_linked_review_gate"),
            patch.object(h38, "capture_preflight", return_value={"status": "ready"}),
            patch.object(h38, "_backup", return_value=dict(baseline)),
            patch.object(h38, "require_run", return_value=(meta, intent)),
            patch.object(h38, "_fresh_current_backup", side_effect=h38.DeadlineError("fresh_backup")),
            patch.object(h38, "_restore_current", side_effect=AssertionError("unverified NVS must not restore")),
            patch.object(h38.h32, "esptool", side_effect=reset_process),
        )
        for item in early_patches:
            item.__enter__()
        try:
            try:
                h38.run_epoch("fake-port", early, root, now=lambda: 0.0,
                              serial_importer=lambda _: object())
            except h38.DeadlineError:
                pass
            else:
                raise AssertionError("fresh-backup timeout was accepted")
            ledger = json.loads((early / "run-result-private.json").read_text())
            check(not ledger["experiment_flash_started"] and not ledger["current_backup_verified"],
                  "early backup failure claims mutation authority")
            check(ledger["device_disposition"] == "loader_held_current_backup_unverified",
                  "early backup failure did not record the held loader state")
            check(early_actions and "default_reset" in early_actions[0],
                  "early backup timeout omitted emergency loader reset")
        finally:
            for item in reversed(early_patches):
                item.__exit__(None, None, None)


def existing_restore_checks():
    from contextlib import ExitStack
    with tempfile.TemporaryDirectory(prefix="h38-existing-restore-") as folder:
        root = Path(folder)
        full = root / "original-flash.bin"
        full.write_bytes(b"existing image")
        baseline = {"_directory": str(root), "full": {"sha256": h38.h32.digest(full)},
                    "partition_table": {"sha256": "a" * 64}, "nvs": {"sha256": "b" * 64},
                    "device": {"fingerprint_sha256": "c" * 64},
                    "apps": [{"name": "factory", "offset": 0, "size": 14,
                              "descriptor": {"project": "original"}, "partition_sha256": h38.h32.digest(full)}]}
        with patch.object(h38.h32, "app_desc", return_value={"project": "original"}):
            target, proof = h38._existing_restore_target(baseline)
        check(target == full and proof["full_sha256"] == h38.h32.digest(full), "wrong existing restore image")
        check(proof["apps"][0]["sha256"] == h38.h32.digest(full), "existing app binding lost")
        for fail in (False, True):
            run = root / ("failure" if fail else "success")
            (run / "build").mkdir(parents=True)
            (run / "build/manifest-private.json").write_text("{}")
            (run / "build/family_link_demo.bin").write_bytes(b"app")
            actions = []
            def device(*_):
                actions.append("identity")
            def tool(_, args, *__):
                check(actions[0] == "identity", "flash preceded fingerprint verification")
                actions.append(args[2])
                if "write_flash" in args and fail:
                    raise RuntimeError("injected flash failure")
                if "read_flash" in args:
                    check(args[3] == hex(h38.h32.APP_OFF), "unexpected current full backup read")
                    Path(args[-1]).write_bytes(b"app")
            with ExitStack() as stack:
                stack.enter_context(patch.object(h38.h32, "app_desc", return_value={"project": "original"}))
                for name, kwargs in (
                    ("private_dir", {"side_effect": lambda p: p}),
                    ("validate", {"return_value": {}}), ("_review_gate", {}),
                    ("_linked_review_gate", {}), ("capture_preflight", {}),
                    ("_backup", {"return_value": dict(baseline)}),
                    ("require_run", {"return_value": ({"baseline_full_sha256": proof["full_sha256"]}, {"epoch": "0" * 32})}),
                    ("_bounded_verify_device", {"side_effect": device}),
                    ("_bounded_esptool", {"side_effect": tool}),
                    ("_fresh_current_backup", {"side_effect": AssertionError("fresh backup forbidden")}),
                    ("_wait_current_backup_review", {"side_effect": AssertionError("current backup review forbidden")}),
                    ("_capture_h38", {"return_value": {"status": "verified"}})):
                    stack.enter_context(patch.object(h38, name, **kwargs))
                restore = stack.enter_context(patch.object(h38, "_restore_current", return_value={"status": "verified"}))
                try:
                    h38.run_epoch("fake", run, root, now=lambda: 0.0,
                                  serial_importer=lambda _: object(), use_existing_restore_image=True)
                except RuntimeError:
                    check(fail, "unexpected failure")
                else:
                    check(not fail, "flash failure accepted")
                check(restore.call_count == 1 and restore.call_args.args[2].resolve() == full.resolve(),
                      "existing image restoration skipped or wrong target")
            result = json.loads((run / "run-result-private.json").read_text())
            check(not result["current_backup_verified"] and not result["current_backup_review_approved"],
                  "existing image mislabeled current backup")
            check(result["current_content_preservation_waived"] and not result["outside_app_erase_interval_readback_checked"],
                  "waived preservation evidence incorrect")
            check(result["restore_source"] == "existing_verified_baseline", "restore source lost")


def main():
    clock_checks()
    clock_freeze_checks()
    short_write_check()
    capture_failure_checks()
    boot_record_checks()
    binding_and_review_checks()
    recovery_readback_checks()
    successful_dispatch_check()
    successful_dispatch_check(crlf=True)
    restore_current_checks()
    recovery_checks()
    existing_restore_checks()
    print("H38 controller host-only checks passed: clocks, transport, drain, mandatory restore")


if __name__ == "__main__":
    main()
