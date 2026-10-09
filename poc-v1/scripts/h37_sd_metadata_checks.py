#!/usr/bin/env python3
"""Synthetic positive/negative checks for the H37 metadata/protocol module."""
from __future__ import annotations

import base64
import hashlib
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from scripts.h37_sd_metadata import (  # noqa: E402
    MAX_BYTES, MetadataClassifier, MetadataError, ProtocolError, ProtocolParser,
    classify, h35_cid_digest, parse_capture,
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def expect_error(fn, label: str, error=Exception) -> None:
    try:
        fn()
    except error:
        return
    raise AssertionError(f"{label}: expected {error.__name__}")


def boot_sector(kind: str = "fat32") -> bytes:
    b = bytearray(512)
    if kind == "fat32": b[82:90] = b"FAT32   "
    elif kind == "fat16": b[54:62] = b"FAT16   "
    elif kind == "ntfs": b[3:11] = b"NTFS    "
    elif kind == "exfat": b[3:11] = b"EXFAT   "
    elif kind == "corrupt_exfat": b[3:11] = b"EXFAT   "
    elif kind != "unknown": raise ValueError(kind)
    if kind != "corrupt_exfat":
        b[510:512] = b"\x55\xaa"
    return bytes(b)


def mbr_sector(protective: bool = False, parts=()) -> bytes:
    b = bytearray(512)
    for i, (kind, start, count) in enumerate(parts):
        struct.pack_into("<B3sB3sII", b, 446 + i * 16, 0, b"\0\0\0", kind,
                         b"\0\0\0", start, count)
    if protective:
        struct.pack_into("<B3sB3sII", b, 446, 0, b"\0\0\0", 0xEE,
                         b"\0\0\0", 1, 4095)
    b[510:512] = b"\x55\xaa"
    return bytes(b)


def gpt_header(current: int, backup: int, entries_lba: int, entries_crc: int,
               first=34, last=4093, count=1, entry_size=128, disk_guid=b"D" * 16) -> bytes:
    b = bytearray(512)
    b[:8] = b"EFI PART"
    struct.pack_into("<IIII", b, 8, 0x00010000, 92, 0, 0)
    struct.pack_into("<QQQQ", b, 24, current, backup, first, last)
    b[56:72] = disk_guid
    struct.pack_into("<QIII", b, 72, entries_lba, count, entry_size, entries_crc)
    crc = zlib.crc32(b[:92]) & 0xFFFFFFFF
    struct.pack_into("<I", b, 16, crc)
    return bytes(b)


def gpt_disk(partition_boot: bytes | None = None) -> dict[int, bytes]:
    entries = bytearray(128)
    entries[:16] = b"T" * 16
    entries[16:32] = b"U" * 16
    struct.pack_into("<QQQ", entries, 32, 2048, 3071, 0)
    crc = zlib.crc32(entries) & 0xFFFFFFFF
    disk = {
        0: mbr_sector(protective=True),
        1: gpt_header(1, 4095, 2, crc),
        2: bytes(entries) + b"\0" * (512 - len(entries)),
        4094: bytes(entries) + b"\0" * (512 - len(entries)),
        4095: gpt_header(4095, 1, 4094, crc),
    }
    if partition_boot is not None:
        disk[2048] = partition_boot
    return disk


def geometry(sectors: int = 4096) -> dict[str, int]:
    return {"sectors": sectors, "sector_bytes": 512, "capacity_bytes": sectors * 512}


def test_classifier() -> int:
    disk = {0: boot_sector("exfat")}
    result = classify(geometry(), lambda lba, count: b"".join(disk[i] for i in range(lba, lba + count)))
    require(result.verdict == "classified" and result.signatures == ("exfat",), "superfloppy exFAT")
    require(result.read_bytes == 512 and result.sanitized()["partitions"] == [], "superfloppy bounds")

    disk = {0: mbr_sector(parts=[(0x0C, 2048, 1024)]), 2048: boot_sector("fat32")}
    result = classify(geometry(), lambda lba, count: b"".join(disk[i] for i in range(lba, lba + count)))
    require(result.verdict == "classified" and result.scheme == "mbr", "MBR FAT32 classification")
    require(result.partitions[0].start_lba == 2048 and result.read_bytes == 1024, "MBR exact extents")

    disk = gpt_disk(boot_sector("ntfs"))
    requested = []
    result = classify(geometry(), lambda lba, count: requested.append((lba, count)) or b"".join(disk[i] for i in range(lba, lba + count)))
    require(result.verdict == "classified" and result.scheme == "gpt", "GPT NTFS classification")
    require(requested[0] == (0, 1) and requested[1] == (1, 1) and requested[2] == (4095, 1), "conditional GPT bootstrap order")
    require(requested[-1] == (2048, 1) and result.read_bytes == len(requested) * 512, "GPT final partition-start probe")
    require(all(1 <= n <= 8 for _, n in requested), "request sector cap")

    # More than 128 GPT entries is explicitly unsupported before array reads.
    p = bytearray(gpt_disk()[1]); struct.pack_into("<I", p, 80, 129); struct.pack_into("<I", p, 16, 0)
    struct.pack_into("<I", p, 16, zlib.crc32(p[:92]) & 0xFFFFFFFF)
    unsupported_disk = {0: mbr_sector(protective=True), 1: bytes(p), 4095: gpt_header(4095, 1, 4063, 0, count=129)}
    result = classify(geometry(), lambda lba, count: b"".join(unsupported_disk[i] for i in range(lba, lba + count)))
    require(result.verdict == "unsupported", "GPT entry-count cap")

    ext = {0: mbr_sector(parts=[(0x0F, 1, 2048)])}
    result = classify(geometry(), lambda lba, count: ext[lba])
    require(result.verdict == "unsupported" and result.read_bytes == 512, "extended MBR unsupported without chain reads")

    bad_sig = {0: boot_sector("corrupt_exfat")}
    result = classify(geometry(), lambda lba, count: bad_sig[lba])
    require(result.verdict == "corrupt", "known filesystem signature without boot-sector marker")

    corrupt = gpt_disk(boot_sector("fat32")); corrupt[2] = b"X" * 512
    result = classify(geometry(), lambda lba, count: b"".join(corrupt[i] for i in range(lba, lba + count)))
    require(result.verdict == "corrupt", "GPT array CRC corruption")

    overlap = {0: mbr_sector(parts=[(0x0C, 10, 100), (0x83, 50, 100)])}
    result = classify(geometry(), lambda lba, count: overlap[lba])
    require(result.verdict == "corrupt", "overlapping MBR partitions")

    overflow = {0: mbr_sector(parts=[(0x0C, 0xFFFFFFF0, 64)])}
    result = classify(geometry(), lambda lba, count: overflow[lba])
    require(result.verdict == "corrupt", "out-of-geometry MBR overflow")

    classifier = MetadataClassifier(geometry())
    requests = classifier.next_requests()
    require([(r.lba, r.count) for r in requests] == [(0, 1)], "only LBA0 bootstrap")
    expect_error(lambda: classifier.next_requests({0: b"0" * 512, 1: b"1" * 512}),
                 "fabricated unrequested snapshot", MetadataError)
    return 11


EPOCH = "a" * 32
ELF = "b" * 64
H35_EPOCH = "c" * 32
CID = {"mfg_id": "1", "oem_id": "2", "revision": "3", "serial": "4", "date": "5",
       "name_size": "3", "name_hex": "414243"}
CID_DIGEST = h35_cid_digest(H35_EPOCH, CID)
PAYLOAD = boot_sector("fat32")
PAYLOAD_SHA = hashlib.sha256(PAYLOAD).hexdigest()
TEST_GEOMETRY = {"sectors": 4096, "sector_bytes": 512, "capacity_bytes": 4096 * 512,
                 "bus_width": 4, "real_freq_khz": 20000, "ddr": 0}


def render(event: str, **fields: object) -> bytes:
    all_fields = {"epoch": EPOCH, "elf_sha256": ELF, **{k: str(v) for k, v in fields.items()}}
    return ("H37,1," + event + "," + ",".join(f"{k}={v}" for k, v in all_fields.items()) + "\n").encode("ascii")


def success_records(repeat_lba: bool = False) -> tuple[bytes, list[tuple[int, int]]]:
    rows = [
        render("BOOT", reset_reason=1),
        render("TRANSPORT", **{
            "profile": "sdmmc_metadata_v1", "backend": "sdmmc", "mode": "read_only",
            "console": "usb_serial_jtag", "usb_host": "disabled", "slot": 0,
            "width_requested": 4, "max_freq_khz": 20000, "command_timeout_ms": 1000,
            "byte_budget": 131072, "max_request_sectors": 8, "command_line_max": 256,
            "record_max": 8192, "session_ms": 240000, "command_ms": 5000,
            "idle_ms": 15000, "discovery_ms": 45000, "usb_rx_bytes": 512, "usb_tx_bytes": 8192,
        }),
        render("POWER", gpio=43, active_level=0, error=0),
        render("HOST", error=0), render("SLOT", error=0), render("CARD", error=0),
        render("GEOMETRY", sectors=4096, sector_bytes=512, capacity_bytes=4096 * 512,
               bus_width=4, real_freq_khz=20000, ddr=0),
        render("CID_PRIVATE", **CID), render("READY", accepts="BIND"),
        render("IDENTITY_MATCH", reference_epoch=H35_EPOCH, match=1, error=0),
    ]
    plan = [(0, 1), (0, 1)] if repeat_lba else [(0, 1)]
    charged = 0
    for seq, (lba, count) in enumerate(plan, 1):
        charged += count * 512
        rows.append(render("READ_RESULT", seq=seq, lba=lba, count=count, charged_total=charged,
                           error=0, elapsed_us=55, len=512, sha256=PAYLOAD_SHA,
                           b64=base64.b64encode(PAYLOAD).decode("ascii")))
    rows += [
        render("CLEANUP", host_deinit_attempted=1, deinit_error=0, power_off_attempted=1, power_off_error=0),
        render("COMPLETE", result="read_complete", failure_stage="none", error=0,
               read_count=len(plan), charged_total=charged, bound=1,
               scope="classification_only", media_writes=0),
    ]
    return b"".join(rows), plan


def test_protocol() -> int:
    raw, plan = success_records(repeat_lba=True)
    capture = parse_capture(raw, EPOCH, ELF, H35_EPOCH, CID_DIGEST, plan,
                            expected_geometry=TEST_GEOMETRY)
    require(capture.verdict == "read_complete" and capture.read_count == 2 and capture.charged_bytes == 1024,
            "valid transcript and intentional repeated LBA")
    require(capture.sanitized()["terminal"]["scope"] == "classification_only", "sanitized transcript")

    noisy_raw = b"ROM startup bytes before application\r\n" + b"H37C,1,BIND,echoed-command\r\n" + raw
    noisy_capture = parse_capture(noisy_raw, EPOCH, ELF, H35_EPOCH, CID_DIGEST, plan,
                                 expected_geometry=TEST_GEOMETRY)
    require(noisy_capture.bytes_seen == len(noisy_raw), "raw capture byte binding")
    require(len(noisy_capture.records) == len(capture.records), "only exact H37 line prefixes parsed")

    tests = 0
    def reject_bytes(b: bytes, label: str, **kwargs):
        try:
            parse_capture(b, EPOCH, ELF, H35_EPOCH, CID_DIGEST, plan,
                          expected_geometry=TEST_GEOMETRY, **kwargs)
        except (ProtocolError, ValueError):
            return
        raise AssertionError(f"{label}: malformed transcript accepted")

    reject_bytes(raw.replace(EPOCH.encode(), b"d" * 32, 1), "wrong epoch"); tests += 1
    reject_bytes(raw.replace(ELF.encode(), b"e" * 64, 1), "wrong ELF"); tests += 1
    reject_bytes(raw.replace(b"sectors=4096,sector_bytes=512", b"sectors=4095,sector_bytes=512", 1),
                 "geometry mismatch"); tests += 1
    reject_bytes(raw[:-1], "truncated terminal"); tests += 1
    reject_bytes(raw + raw.splitlines(keepends=True)[-1], "duplicate terminal"); tests += 1
    reject_bytes(raw.replace(b"seq=2", b"seq=1"), "duplicate sequence"); tests += 1
    reject_bytes(raw.replace(b"charged_total=1024", b"charged_total=512"), "wrong cumulative charge"); tests += 1
    reject_bytes(raw.replace(base64.b64encode(PAYLOAD), b"%%"), "malformed base64"); tests += 1
    try:
        parse_capture(raw, EPOCH, ELF, H35_EPOCH, "f" * 64, plan, dispatched_reads=plan,
                      expected_geometry=TEST_GEOMETRY)
    except ProtocolError:
        pass
    else:
        raise AssertionError("wrong host identity: transcript accepted")
    tests += 1
    reject_bytes(raw.replace(b"match=1", b"match=0", 1), "false identity match"); tests += 1
    reject_bytes(raw + b"H37,1,HOST," + b"x" * 8200 + b"\n", "oversized record"); tests += 1
    reject_bytes(raw.replace(b"scope=classification_only", b"failure_stage=none,scope=classification_only", 1),
                 "duplicate terminal field"); tests += 1
    reject_bytes(raw, "read outside plan", dispatched_reads=[(9, 1), (0, 1)]); tests += 1
    try:
        parse_capture(b"ROM\nH37BROKEN,1,BOOT\n", EPOCH, ELF, H35_EPOCH,
                      CID_DIGEST, plan, expected_geometry=TEST_GEOMETRY)
    except ProtocolError:
        pass
    else:
        raise AssertionError("malformed H37-like line accepted")
    tests += 1

    parser = ProtocolParser(EPOCH, ELF, H35_EPOCH, CID_DIGEST)
    expect_error(lambda: parser.set_read_plan([(0, 8)] * 33), "oversized read plan", ProtocolError); tests += 1
    expect_error(lambda: h35_cid_digest(H35_EPOCH, {**CID, "name_size": "17", "name_hex": "00" * 17}),
                 "oversized CID name", ProtocolError); tests += 1

    # A timeout with one dispatched but unanswered command remains charged.
    base = success_records()[0]
    lines = base.splitlines(keepends=True)
    lines = [line for line in lines if b"READ_RESULT" not in line]
    lines[-1] = render("COMPLETE", result="failed", failure_stage="timeout", error=110,
                       read_count=1, charged_total=512, bound=1,
                       scope="classification_only", media_writes=0)
    timeout_raw = b"".join(lines)
    timeout_capture = parse_capture(timeout_raw, EPOCH, ELF, H35_EPOCH, CID_DIGEST,
                                    [(0, 1)], dispatched_reads=[(0, 1)],
                                    expected_geometry=TEST_GEOMETRY)
    require(timeout_capture.verdict is None and timeout_capture.charged_bytes == 512,
            "timeout request charged without response")
    return tests


def test_signed_failures() -> int:
    def terminal(stage: str, error: int, count: int = 0, charged: int = 0, bound: int = 0) -> bytes:
        return render("COMPLETE", result="failed", failure_stage=stage, error=error,
                      read_count=count, charged_total=charged, bound=bound,
                      scope="classification_only", media_writes=0)

    cleanup = render("CLEANUP", host_deinit_attempted=1, deinit_error=0,
                     power_off_attempted=1, power_off_error=0)
    success, plan = success_records()
    successful_lines = success.splitlines(keepends=True)
    cases = []
    # Before POWER/HOST initialization, neither cleanup action applies.
    early_cleanup = render("CLEANUP", host_deinit_attempted=0, deinit_error=0,
                           power_off_attempted=0, power_off_error=0)
    for stage in ("resources", "reset"):
        cases.append((b"".join((render("BOOT", reset_reason=1), successful_lines[1], early_cleanup,
                                terminal(stage, -1))), CID_DIGEST, [], None))

    # Closed-stage driver failures must agree with the matching event error.
    for stage, event in (("power", "POWER"), ("host", "HOST"),
                         ("slot", "SLOT"), ("card", "CARD")):
        prefix = [line for line in successful_lines if line.split(b",", 3)[2] != b"COMPLETE"]
        prefix = [line for line in prefix if line.split(b",", 3)[2] in
                  {b"BOOT", b"TRANSPORT", b"POWER", b"HOST", b"SLOT", b"CARD"}]
        found = False
        for i, line in enumerate(prefix):
            if line.split(b",", 3)[2] == event.encode():
                prefix[i] = line.replace(b"error=0", b"error=-1")
                prefix = prefix[:i + 1]
                found = True
                break
        require(found, f"failure fixture event {event}")
        if stage == "power":
            # POWER v1 cannot tell gpio_config failure (no resource acquired)
            # from initial gpio_set_level failure (output configured). Both
            # observed cleanup shapes are covered; this remains a source/ABI
            # ambiguity until POWER reports configured state.
            cases.append((b"".join(prefix) + early_cleanup + terminal(stage, -1), CID_DIGEST, [], None))
            power_configured_cleanup = render("CLEANUP", host_deinit_attempted=0, deinit_error=0,
                                              power_off_attempted=1, power_off_error=0)
            cases.append((b"".join(prefix) + power_configured_cleanup + terminal(stage, -1), CID_DIGEST, [], None))
        elif stage == "host":
            host_failed_cleanup = render("CLEANUP", host_deinit_attempted=0, deinit_error=0,
                                         power_off_attempted=1, power_off_error=0)
            cases.append((b"".join(prefix) + host_failed_cleanup + terminal(stage, -1), CID_DIGEST, [], None))
        else:
            cases.append((b"".join(prefix) + cleanup + terminal(stage, -1), CID_DIGEST, [], None))

    wrong_binding = "f" * 64
    bind_lines = successful_lines[:10]
    bind_lines[-1] = bind_lines[-1].replace(b"match=1,error=0", b"match=0,error=-1")
    cases.append((b"".join(bind_lines) + cleanup + terminal("bind", -1), wrong_binding, [], None))

    read_line = render("READ_RESULT", seq=1, lba=0, count=1, charged_total=512,
                       error=-1, elapsed_us=50, len=0, sha256="none", b64="none")
    read_prefix = b"".join(successful_lines[:10]) + read_line + cleanup + terminal("read", -1, 1, 512, 1)
    cases.append((read_prefix, CID_DIGEST, [(0, 1)], [(0, 1)]))

    extra_checks = 0
    success_terminal_line = successful_lines[-1]
    cleanup_bad = render("CLEANUP", host_deinit_attempted=1, deinit_error=-1,
                         power_off_attempted=1, power_off_error=0)
    cleanup_prefix = b"".join(successful_lines[:-2]) + cleanup_bad + terminal("cleanup", -1, 1, 512, 1)
    cases.append((cleanup_prefix, CID_DIGEST, [(0, 1)], [(0, 1)]))
    expect_error(lambda: parse_capture(b"".join(successful_lines[:-2]) + cleanup_bad +
                                       terminal("timeout", -1, 1, 512, 1),
                                       EPOCH, ELF, H35_EPOCH, CID_DIGEST, [(0, 1)],
                                       dispatched_reads=[(0, 1)], expected_geometry=TEST_GEOMETRY),
                 "cleanup error cannot be relabeled as another terminal stage", ProtocolError)
    extra_checks += 1
    power_failure_prefix = list(successful_lines)
    power_idx = next(i for i, line in enumerate(power_failure_prefix)
                     if line.split(b",", 3)[2] == b"POWER")
    power_failure_prefix = power_failure_prefix[:power_idx + 1]
    power_failure_prefix[power_idx] = power_failure_prefix[power_idx].replace(b"error=0", b"error=-1")
    expect_error(lambda: parse_capture(b"".join(power_failure_prefix) + early_cleanup +
                                       terminal("timeout", -1),
                                       EPOCH, ELF, H35_EPOCH, CID_DIGEST, []),
                 "POWER failure cannot be relabeled as timeout", ProtocolError)
    extra_checks += 1
    both_cleanup_bad = render("CLEANUP", host_deinit_attempted=1, deinit_error=-1,
                              power_off_attempted=1, power_off_error=-2)
    expect_error(lambda: parse_capture(b"".join(successful_lines[:-2]) + both_cleanup_bad +
                                       terminal("cleanup", -1, 1, 512, 1),
                                       EPOCH, ELF, H35_EPOCH, CID_DIGEST, [(0, 1)],
                                       dispatched_reads=[(0, 1)], expected_geometry=TEST_GEOMETRY),
                 "last cleanup failure determines terminal error", ProtocolError)
    extra_checks += 1

    # Reject attempts inconsistent with positively observed acquisition.
    host_omitted = render("CLEANUP", host_deinit_attempted=0, deinit_error=0,
                          power_off_attempted=1, power_off_error=0)
    expect_error(lambda: parse_capture(b"".join(successful_lines[:-2]) + host_omitted +
                                       terminal("timeout", -1, 1, 512, 1),
                                       EPOCH, ELF, H35_EPOCH, CID_DIGEST, [(0, 1)],
                                       dispatched_reads=[(0, 1)], expected_geometry=TEST_GEOMETRY),
                 "host cleanup omitted after successful host init", ProtocolError)
    power_omitted = render("CLEANUP", host_deinit_attempted=1, deinit_error=0,
                           power_off_attempted=0, power_off_error=0)
    expect_error(lambda: parse_capture(b"".join(successful_lines[:-2]) + power_omitted +
                                       terminal("timeout", -1, 1, 512, 1),
                                       EPOCH, ELF, H35_EPOCH, CID_DIGEST, [(0, 1)],
                                       dispatched_reads=[(0, 1)], expected_geometry=TEST_GEOMETRY),
                 "power cleanup omitted after successful setup", ProtocolError)

    # Stage-bound failures with signed ESP_FAIL must parse, but have no FS verdict.
    for raw, expected_digest, read_plan, dispatched in cases:
        capture = parse_capture(raw, EPOCH, ELF, H35_EPOCH, expected_digest,
                                read_plan, dispatched_reads=dispatched,
                                expected_geometry=TEST_GEOMETRY)
        require(capture.verdict is None and capture.terminal["result"] == "failed",
                "failed capture must not produce filesystem verdict")
    # POWER failure ambiguity is permitted only for a failed terminal. Neither
    # cleanup shape may turn a failed POWER event into a read_complete result.
    for attempted in (0, 1):
        invalid_success = list(successful_lines)
        power_index = next(i for i, line in enumerate(invalid_success)
                           if line.split(b",", 3)[2] == b"POWER")
        invalid_success[power_index] = invalid_success[power_index].replace(b"error=0", b"error=-1")
        invalid_success[-2] = render("CLEANUP", host_deinit_attempted=1, deinit_error=0,
                                     power_off_attempted=attempted, power_off_error=0)
        expect_error(lambda raw=b"".join(invalid_success):
                     parse_capture(raw, EPOCH, ELF, H35_EPOCH, CID_DIGEST, plan,
                                   expected_geometry=TEST_GEOMETRY),
                     f"POWER error with cleanup attempt {attempted} cannot be successful", ProtocolError)
        extra_checks += 1
    # Attempts for resources that were never acquired must be rejected too.
    bad_early_cleanup = render("CLEANUP", host_deinit_attempted=1, deinit_error=0,
                              power_off_attempted=1, power_off_error=0)
    bad_early = b"".join((render("BOOT", reset_reason=1), successful_lines[1], bad_early_cleanup,
                          terminal("resources", -1)))
    expect_error(lambda: parse_capture(bad_early, EPOCH, ELF, H35_EPOCH, CID_DIGEST, []),
                 "early failure cannot claim unacquired cleanup resources", ProtocolError)
    return len(cases) + extra_checks


def main() -> int:
    checks = test_classifier() + test_protocol() + test_signed_failures()
    print(f"H37 synthetic checks passed: {checks}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
