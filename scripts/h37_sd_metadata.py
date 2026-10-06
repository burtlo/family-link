#!/usr/bin/env python3
"""Bounded H37 SD metadata classifier and strict H37 v1 transcript parser.

This module never mounts a filesystem and never returns filesystem contents,
names, partition labels, raw sector bytes, or private identity digests.
"""
from __future__ import annotations

import base64
import binascii
import hashlib
import hmac
import json
import re
import struct
import zlib
from dataclasses import dataclass
from typing import Callable, Iterable, Mapping, Sequence

MAX_BYTES = 128 * 1024
MAX_SECTORS_PER_READ = 8
SECTOR_BYTES = 512
MAX_GPT_ENTRIES = 128
MAX_RECORD_BYTES = 8192
MAX_CAPTURE_BYTES = 1024 * 1024
FAILURE_STAGES = {
    "none", "resources", "reset", "power", "host", "slot", "card",
    "geometry", "bind", "command", "read", "timeout", "cleanup",
}
TRANSPORT_FIELDS = {
    "profile": "sdmmc_metadata_v1", "backend": "sdmmc",
    "mode": "read_only", "console": "usb_serial_jtag",
    "usb_host": "disabled", "slot": "0", "width_requested": "4",
    "max_freq_khz": "20000", "command_timeout_ms": "1000",
    "byte_budget": "131072", "max_request_sectors": "8",
    "command_line_max": "256", "record_max": "8192",
    "session_ms": "240000", "command_ms": "5000", "idle_ms": "15000",
    "discovery_ms": "45000", "usb_rx_bytes": "512", "usb_tx_bytes": "8192",
}
EXPECTED_CARD_GEOMETRY = {
    "sectors": 121503744, "sector_bytes": 512, "capacity_bytes": 62209916928,
    "bus_width": 4, "real_freq_khz": 20000, "ddr": 0,
}
EVENT_FIELDS = {
    "BOOT": {"reset_reason"},
    "TRANSPORT": set(TRANSPORT_FIELDS),
    "POWER": {"gpio", "active_level", "error"},
    "HOST": {"error"},
    "SLOT": {"error"},
    "CARD": {"error"},
    "GEOMETRY": {"sectors", "sector_bytes", "capacity_bytes", "bus_width", "real_freq_khz", "ddr"},
    "CID_PRIVATE": {"mfg_id", "oem_id", "revision", "serial", "date", "name_size", "name_hex"},
    "READY": {"accepts"},
    "IDENTITY_MATCH": {"reference_epoch", "match", "error"},
    "READ_RESULT": {"seq", "lba", "count", "charged_total", "error", "elapsed_us", "len", "sha256", "b64"},
    "CLEANUP": {"host_deinit_attempted", "deinit_error", "power_off_attempted", "power_off_error"},
    "COMPLETE": {"result", "failure_stage", "error", "read_count", "charged_total", "bound", "scope", "media_writes"},
}
EVENT_ORDER = {name: i for i, name in enumerate((
    "BOOT", "TRANSPORT", "POWER", "HOST", "SLOT", "CARD", "GEOMETRY",
    "CID_PRIVATE", "READY", "IDENTITY_MATCH", "READ_RESULT", "CLEANUP", "COMPLETE",
))}
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
UINT = re.compile(r"(?:0|[1-9][0-9]*)\Z")
SINT = re.compile(r"(?:0|[1-9][0-9]*|-[1-9][0-9]*)\Z")


class MetadataError(ValueError):
    """Malformed metadata, unsafe extent, or unsupported geometry."""


class ProtocolError(ValueError):
    """Malformed, unsafe, incomplete, or inconsistent H37 transcript."""


def _uint(value: str, what: str, maximum: int = 0xFFFFFFFFFFFFFFFF) -> int:
    if not isinstance(value, str) or not UINT.fullmatch(value):
        raise ProtocolError(f"{what} must be canonical unsigned decimal")
    n = int(value)
    if n > maximum:
        raise ProtocolError(f"{what} out of range")
    return n


def _error(value: str, what: str) -> int:
    """Parse a canonical ESP-IDF signed esp_err_t (int32) decimal value."""
    if not isinstance(value, str) or not SINT.fullmatch(value):
        raise ProtocolError(f"{what} must be canonical signed decimal")
    n = int(value)
    if not -(1 << 31) <= n < (1 << 31):
        raise ProtocolError(f"{what} outside signed int32")
    return n


def h35_cid_digest(reference_epoch: str, cid: Mapping[str, object]) -> str:
    """Reproduce the committed H35 snprintf/SHA-256 identity serialization."""
    if not re.fullmatch(r"[0-9a-f]{32}", reference_epoch):
        raise ProtocolError("invalid H35 reference epoch")
    try:
        mfg = int(cid["mfg_id"])
        oem = int(cid["oem_id"])
        revision = int(cid["revision"])
        serial = int(cid["serial"])
        date = int(cid["date"])
        name_size = int(cid["name_size"])
        name_hex = str(cid["name_hex"])
    except (KeyError, TypeError, ValueError) as exc:
        raise ProtocolError("incomplete H35 CID binding") from exc
    values = (mfg, oem, revision, serial, date)
    if any(v < 0 or v > 0xFFFFFFFF for v in values):
        raise ProtocolError("CID uint32 out of range")
    if not 0 <= name_size <= 16 or len(name_hex) != name_size * 2 or not re.fullmatch(r"[0-9a-fA-F]*", name_hex):
        raise ProtocolError("CID name encoding invalid")
    name = bytes.fromhex(name_hex).split(b"\0", 1)[0]
    serialized = (
        reference_epoch.encode("ascii") + b":" + f"{mfg:08x}:{oem:08x}:".encode("ascii")
        + name + b":" + f"{revision:08x}:{serial:08x}:{date:08x}".encode("ascii")
    )
    return hashlib.sha256(serialized).hexdigest()


@dataclass(frozen=True)
class ReadRequest:
    lba: int
    count: int
    stage: str

    def as_tuple(self) -> tuple[int, int]:
        return self.lba, self.count


@dataclass(frozen=True)
class Partition:
    start_lba: int
    end_lba: int
    scheme: str
    type_code: str


@dataclass(frozen=True)
class ClassificationResult:
    verdict: str
    scheme: str
    signatures: tuple[str, ...]
    partitions: tuple[Partition, ...]
    planned_reads: tuple[ReadRequest, ...]
    read_bytes: int

    def sanitized(self) -> dict[str, object]:
        """Return allowlisted metadata only; no names, GUIDs, hashes, or sectors."""
        return {
            "verdict": self.verdict,
            "scheme": self.scheme,
            "signatures": list(self.signatures),
            "partitions": [
                {"start_lba": p.start_lba, "end_lba": p.end_lba,
                 "scheme": p.scheme, "type_code": p.type_code}
                for p in self.partitions
            ],
            "read_count": len(self.planned_reads),
            "read_bytes": self.read_bytes,
        }


def _geometry(value: Mapping[str, object]) -> tuple[int, int]:
    try:
        sectors = int(value["sectors"])
        sector_bytes = int(value["sector_bytes"])
        capacity = int(value.get("capacity_bytes", sectors * sector_bytes))
    except (KeyError, TypeError, ValueError) as exc:
        raise MetadataError("geometry must contain integer sectors and sector_bytes") from exc
    if sectors < 1 or sector_bytes != SECTOR_BYTES or capacity != sectors * sector_bytes:
        raise MetadataError("unsupported or inconsistent geometry")
    return sectors, sector_bytes


def _sector(snapshot: Mapping[int, bytes], lba: int) -> bytes | None:
    data = snapshot.get(lba)
    if data is None:
        return None
    if not isinstance(data, bytes) or len(data) != SECTOR_BYTES:
        raise MetadataError(f"sector {lba} is not exactly 512 bytes")
    return data


def _chunks(start: int, count: int, stage: str) -> list[ReadRequest]:
    out = []
    while count:
        n = min(count, MAX_SECTORS_PER_READ)
        out.append(ReadRequest(start, n, stage))
        start += n
        count -= n
    return out


def _ranges_disjoint(ranges: Sequence[tuple[int, int]], label: str) -> None:
    ordered = sorted(ranges)
    for previous, current in zip(ordered, ordered[1:]):
        if current[0] <= previous[1]:
            raise MetadataError(f"overlapping {label}")


def _mbr(sector: bytes, sectors: int) -> tuple[list[Partition], bool, str]:
    if sector[510:512] != b"\x55\xaa":
        # A recognized superfloppy signature is checked by the caller.
        return [], False, "no_mbr_signature"
    parts: list[Partition] = []
    protective = False
    saw_entries = False
    ext_types = {0x05, 0x0F, 0x85}
    for i in range(4):
        raw = sector[446 + 16 * i:462 + 16 * i]
        status, _chs0, kind, _chs1, start, length = struct.unpack("<B3sB3sII", raw)
        if status not in (0, 0x80):
            raise MetadataError("invalid MBR partition status")
        if kind == 0 and start == 0 and length == 0:
            continue
        if kind == 0 or start == 0 or length == 0:
            raise MetadataError("partial MBR partition entry")
        saw_entries = True
        end = start + length - 1
        if end < start or end >= sectors:
            raise MetadataError("MBR partition outside geometry")
        if kind == 0xEE:
            protective = True
            parts.append(Partition(start, end, "mbr", "ee"))
        elif kind in ext_types:
            raise UnsupportedLayout("extended MBR partition chains are excluded")
        else:
            parts.append(Partition(start, end, "mbr", f"{kind:02x}"))
    _ranges_disjoint([(p.start_lba, p.end_lba) for p in parts], "MBR partitions")
    if protective and (len(parts) != 1 or parts[0].start_lba != 1):
        raise MetadataError("protective MBR contains unexpected entries")
    return parts, protective, "mbr" if saw_entries else "empty_mbr"


class UnsupportedLayout(MetadataError):
    """Valid but deliberately unsupported partition layout."""


@dataclass(frozen=True)
class _GPTHeader:
    current: int
    backup: int
    first_usable: int
    last_usable: int
    disk_guid: bytes
    entries_lba: int
    entry_count: int
    entry_size: int
    entries_crc: int
    header_size: int


def _gpt_header(sector: bytes, sectors: int, expected_current: int) -> _GPTHeader:
    if sector[:8] != b"EFI PART":
        raise MetadataError("GPT header signature missing")
    revision, header_size, header_crc, reserved = struct.unpack_from("<IIII", sector, 8)
    if revision < 0x00010000 or not 92 <= header_size <= SECTOR_BYTES or reserved != 0:
        raise MetadataError("invalid GPT header dimensions")
    buf = bytearray(sector[:header_size])
    struct.pack_into("<I", buf, 16, 0)
    if zlib.crc32(buf) & 0xFFFFFFFF != header_crc:
        raise MetadataError("GPT header CRC mismatch")
    current, backup, first, last = struct.unpack_from("<QQQQ", sector, 24)
    guid = sector[56:72]
    entries_lba, count, entry_size, entries_crc = struct.unpack_from("<QIII", sector, 72)
    if current != expected_current or backup >= sectors or backup == current:
        raise MetadataError("GPT reciprocal header LBAs invalid")
    if first > last or last >= sectors or first < 2:
        raise MetadataError("GPT usable-LBA range invalid")
    if count < 1 or count > MAX_GPT_ENTRIES or entry_size != 128:
        raise UnsupportedLayout("GPT entry shape exceeds reviewed bounds")
    array_sectors = (count * entry_size + SECTOR_BYTES - 1) // SECTOR_BYTES
    if entries_lba >= sectors or array_sectors > sectors - entries_lba:
        raise MetadataError("GPT entry array outside geometry")
    if entries_lba <= current < entries_lba + array_sectors:
        raise MetadataError("GPT entry array overlaps header")
    return _GPTHeader(current, backup, first, last, guid, entries_lba,
                      count, entry_size, entries_crc, header_size)


def _read_array(snapshot: Mapping[int, bytes], header: _GPTHeader) -> bytes | None:
    nbytes = header.entry_count * header.entry_size
    nsectors = (nbytes + SECTOR_BYTES - 1) // SECTOR_BYTES
    chunks = []
    for lba in range(header.entries_lba, header.entries_lba + nsectors):
        sector = _sector(snapshot, lba)
        if sector is None:
            return None
        chunks.append(sector)
    data = b"".join(chunks)[:nbytes]
    if zlib.crc32(data) & 0xFFFFFFFF != header.entries_crc:
        raise MetadataError("GPT entry-array CRC mismatch")
    return data


def _gpt_partitions(array: bytes, header: _GPTHeader) -> list[Partition]:
    parts: list[Partition] = []
    unique_guids: set[bytes] = set()
    for i in range(header.entry_count):
        e = array[i * 128:(i + 1) * 128]
        type_guid, unique_guid = e[:16], e[16:32]
        if type_guid == b"\0" * 16:
            continue
        first, last, _attrs = struct.unpack_from("<QQQ", e, 32)
        if unique_guid == b"\0" * 16 or first > last:
            raise MetadataError("invalid GPT partition identity or bounds")
        if unique_guid in unique_guids:
            raise MetadataError("duplicate GPT partition GUID")
        unique_guids.add(unique_guid)
        if first < header.first_usable or last > header.last_usable:
            raise MetadataError("GPT partition outside usable range")
        parts.append(Partition(first, last, "gpt", type_guid.hex()))
    _ranges_disjoint([(p.start_lba, p.end_lba) for p in parts], "GPT partitions")
    return parts


class MetadataClassifier:
    """Incremental staged planner; call next_requests after each read batch."""

    def __init__(self, geometry: Mapping[str, object]):
        self.sectors, self.sector_bytes = _geometry(geometry)
        self.geometry = dict(geometry)
        self._issued: list[ReadRequest] = []
        self._awaiting: list[ReadRequest] = []
        self._snapshot: dict[int, bytes] = {}
        self._final: ClassificationResult | None = None

    @property
    def planned_bytes(self) -> int:
        return sum(r.count * self.sector_bytes for r in self._issued)

    def _issue(self, requests: Iterable[ReadRequest]) -> list[ReadRequest]:
        out = list(requests)
        if self._awaiting:
            raise MetadataError("previous read stage has not been fully accepted")
        if any(r.lba < 0 or r.count < 1 or r.count > MAX_SECTORS_PER_READ or r.lba + r.count > self.sectors for r in out):
            raise MetadataError("read plan outside geometry or request-size limit")
        if self.planned_bytes + sum(r.count * self.sector_bytes for r in out) > MAX_BYTES:
            raise MetadataError("128-KiB metadata read budget exceeded")
        self._issued.extend(out)
        self._awaiting.extend(out)
        return out

    def _mbr_sector(self, snapshot: Mapping[int, bytes]) -> bytes:
        s = _sector(snapshot, 0)
        if s is None:
            raise MetadataError("bootstrap MBR sector missing")
        return s

    def _validate_snapshot(self, snapshot: Mapping[int, bytes]) -> None:
        allowed = {lba for request in self._issued for lba in range(request.lba, request.lba + request.count)}
        if any(not isinstance(lba, int) or lba not in allowed for lba in snapshot):
            raise MetadataError("sector snapshot contains data not authorized by a dispatched plan")
        for lba in snapshot:
            _sector(snapshot, lba)

    def _terminal(self, verdict: str, scheme: str, signatures: Sequence[str] = (),
                  partitions: Sequence[Partition] = ()) -> list[ReadRequest]:
        self._final = ClassificationResult(verdict, scheme, tuple(signatures),
                                           tuple(partitions), tuple(self._issued), self.planned_bytes)
        return []

    @property
    def sector_map(self) -> Mapping[int, bytes]:
        """Return a copy of sectors accepted from validated successful records."""
        return dict(self._snapshot)

    def accept_record(self, record: "ParsedRecord") -> None:
        """Accept only the next successful read returned by the strict parser."""
        if not isinstance(record, ParsedRecord) or record.event != "READ_RESULT":
            raise MetadataError("only a parsed READ_RESULT can supply metadata bytes")
        if not self._awaiting:
            raise MetadataError("READ_RESULT has no outstanding metadata plan")
        request = self._awaiting[0]
        f = record.fields
        try:
            lba, count, error = int(f["lba"]), int(f["count"]), int(f["error"])
            raw = base64.b64decode(f["b64"], validate=True)
        except (KeyError, TypeError, ValueError, binascii.Error) as exc:
            raise MetadataError("validated READ_RESULT fields unavailable") from exc
        if (lba, count) != request.as_tuple() or error != 0:
            raise MetadataError("failed or unexpected read cannot feed filesystem classification")
        if len(raw) != count * SECTOR_BYTES or hashlib.sha256(raw).hexdigest() != f.get("sha256"):
            raise MetadataError("READ_RESULT payload integrity mismatch")
        for i in range(count):
            sector_lba = lba + i
            if sector_lba in self._snapshot:
                raise MetadataError("overlapping sector data in classifier snapshot")
            self._snapshot[sector_lba] = raw[i * SECTOR_BYTES:(i + 1) * SECTOR_BYTES]
        self._awaiting.pop(0)

    def _accept_bytes(self, request: ReadRequest, raw: bytes) -> None:
        if not self._awaiting or self._awaiting[0] != request:
            raise MetadataError("callback data differs from current metadata plan")
        if not isinstance(raw, bytes) or len(raw) != request.count * SECTOR_BYTES:
            raise MetadataError("sector callback returned an error or wrong byte count")
        for i in range(request.count):
            self._snapshot[request.lba + i] = raw[i * SECTOR_BYTES:(i + 1) * SECTOR_BYTES]
        self._awaiting.pop(0)

    def next_requests(self, snapshot: Mapping[int, bytes] | None = None) -> list[ReadRequest]:
        """Return only the next structurally justified and bounded read stage."""
        if snapshot is not None and dict(snapshot) != self._snapshot:
            raise MetadataError("caller snapshot differs from validated READ_RESULT data")
        snapshot = self._snapshot
        self._validate_snapshot(snapshot)
        if self._final is not None:
            return []
        if self._awaiting:
            raise MetadataError("current staged reads have not all been accepted")
        if 0 not in snapshot:
            if not self._issued:
                return self._issue([ReadRequest(0, 1, "mbr")])
            raise MetadataError("bootstrap MBR sector missing after dispatch")
        mbr = self._mbr_sector(snapshot)
        boot_sig = _boot_signature(mbr)
        if boot_sig.startswith("corrupt_"):
            return self._terminal("corrupt", "superfloppy", (boot_sig,))
        if boot_sig != "unknown":
            return self._terminal("classified", "superfloppy", (boot_sig,))
        try:
            mbr_parts, protective, scheme = _mbr(mbr, self.sectors)
        except UnsupportedLayout as exc:
            return self._terminal("unsupported", "mbr", ("unsupported_layout",))
        except MetadataError as exc:
            return self._terminal("corrupt", "mbr", ("corrupt_mbr",))
        if not protective:
            if not mbr_parts:
                return self._terminal("unknown", "empty_mbr", ("unknown",))
            pending = [ReadRequest(p.start_lba, 1, "partition_boot") for p in mbr_parts if p.start_lba not in snapshot]
            if pending:
                return self._issue(pending)
            sigs = tuple(_boot_signature(_sector(snapshot, p.start_lba) or b"") for p in mbr_parts)
            verdict = _signature_verdict(sigs)
            self._final = ClassificationResult(verdict, scheme, sigs, tuple(mbr_parts), tuple(self._issued), self.planned_bytes)
            return []

        # LBA 1 and last-sector header are requested only after protective MBR.
        probe_headers = [1, self.sectors - 1]
        pending = [ReadRequest(lba, 1, "gpt_headers") for lba in probe_headers if lba not in snapshot]
        if pending:
            return self._issue(pending)
        backup_lba = self.sectors - 1
        try:
            primary = _gpt_header(_sector(snapshot, 1) or b"", self.sectors, 1)
            if primary.backup != backup_lba:
                raise UnsupportedLayout("noncanonical backup GPT location")
            backup = _gpt_header(_sector(snapshot, backup_lba) or b"", self.sectors, backup_lba)
            _validate_gpt_pair(primary, backup, self.sectors)
        except UnsupportedLayout as exc:
            return self._terminal("unsupported", "gpt", ("unsupported_gpt",))
        except MetadataError as exc:
            return self._terminal("corrupt", "gpt", ("corrupt_gpt",))

        array_sectors = (primary.entry_count * primary.entry_size + self.sector_bytes - 1) // self.sector_bytes
        array_requests = _chunks(primary.entries_lba, array_sectors, "gpt_primary_array")
        backup_requests = _chunks(backup.entries_lba, array_sectors, "gpt_backup_array")
        missing = [r for r in array_requests + backup_requests
                   if any(lba not in snapshot for lba in range(r.lba, r.lba + r.count))]
        if missing:
            # Existing bootstrap/header sectors may coincide with an array; split to missing runs.
            return self._issue(_missing_chunks(snapshot, array_requests + backup_requests))
        try:
            primary_array = _read_array(snapshot, primary)
            backup_array = _read_array(snapshot, backup)
            assert primary_array is not None and backup_array is not None
            if primary_array != backup_array:
                raise MetadataError("primary/backup GPT arrays differ")
            parts = _gpt_partitions(primary_array, primary)
        except UnsupportedLayout as exc:
            return self._terminal("unsupported", "gpt", ("unsupported_gpt",))
        except MetadataError as exc:
            return self._terminal("corrupt", "gpt", ("corrupt_gpt",))
        pending = [ReadRequest(p.start_lba, 1, "partition_boot") for p in parts if p.start_lba not in snapshot]
        if pending:
            return self._issue(pending)
        signatures = tuple(_boot_signature(_sector(snapshot, p.start_lba) or b"") for p in parts)
        verdict = _signature_verdict(signatures)
        self._final = ClassificationResult(verdict, "gpt", signatures, tuple(parts), tuple(self._issued), self.planned_bytes)
        return []

    def result(self, snapshot: Mapping[int, bytes] | None = None) -> ClassificationResult:
        if self.next_requests(snapshot):
            raise MetadataError("classification read plan is incomplete")
        if self._final is None:
            raise MetadataError("classification did not reach a terminal result")
        return self._final


def _missing_chunks(snapshot: Mapping[int, bytes], requests: Sequence[ReadRequest]) -> list[ReadRequest]:
    """Return absent sectors as bounded contiguous requests, preserving plan order."""
    out: list[ReadRequest] = []
    for req in requests:
        run_start = None
        run_count = 0
        for lba in range(req.lba, req.lba + req.count):
            if lba not in snapshot:
                if run_start is None:
                    run_start = lba
                run_count += 1
                if run_count == MAX_SECTORS_PER_READ:
                    out.append(ReadRequest(run_start, run_count, req.stage))
                    run_start = None
                    run_count = 0
            elif run_start is not None:
                out.append(ReadRequest(run_start, run_count, req.stage))
                run_start = None
                run_count = 0
        if run_start is not None:
            out.append(ReadRequest(run_start, run_count, req.stage))
    return out


def _validate_gpt_pair(p: _GPTHeader, b: _GPTHeader, sectors: int) -> None:
    if b.current != p.backup or b.backup != p.current:
        raise MetadataError("GPT headers are not reciprocal")
    if (p.first_usable, p.last_usable, p.disk_guid, p.entry_count, p.entry_size, p.entries_crc) != (
        b.first_usable, b.last_usable, b.disk_guid, b.entry_count, b.entry_size, b.entries_crc
    ):
        raise MetadataError("GPT primary/backup headers disagree")
    count = (p.entry_count * p.entry_size + SECTOR_BYTES - 1) // SECTOR_BYTES
    if p.entries_lba < 2 or p.entries_lba + count > p.first_usable:
        raise MetadataError("primary GPT array overlaps usable area")
    if b.entries_lba <= b.last_usable or b.entries_lba + count > b.current:
        raise MetadataError("backup GPT array overlaps usable area/header")
    if p.entries_lba + count > sectors or b.entries_lba + count > sectors:
        raise MetadataError("GPT arrays outside geometry")
    if p.entries_lba < b.entries_lba + count and b.entries_lba < p.entries_lba + count:
        raise MetadataError("primary and backup GPT arrays overlap")


def _boot_signature(data: bytes) -> str:
    if len(data) != SECTOR_BYTES:
        return "unknown"
    signed = data[510:512] == b"\x55\xaa"
    if data[3:11] == b"EXFAT   ":
        return "exfat" if signed else "corrupt_exfat_signature"
    if data[3:11] == b"NTFS    ":
        return "ntfs" if signed else "corrupt_ntfs_signature"
    if data[82:90] == b"FAT32   ":
        return "fat32" if signed else "corrupt_fat32_signature"
    if data[54:62] in (b"FAT12   ", b"FAT16   "):
        return data[54:59].decode("ascii").lower() if signed else "corrupt_fat_signature"
    return "unknown"


def _signature_verdict(signatures: Sequence[str]) -> str:
    if any(s.startswith("corrupt_") for s in signatures):
        return "corrupt"
    if any(s == "unknown" for s in signatures):
        return "unknown"
    return "classified"


def classify(geometry: Mapping[str, object], read_sectors: Callable[[int, int], bytes]) -> ClassificationResult:
    """Convenience two-phase reader; live controllers should persist each plan stage."""
    classifier = MetadataClassifier(geometry)
    while True:
        requests = classifier.next_requests()
        if not requests:
            return classifier.result()
        for req in requests:
            result = read_sectors(req.lba, req.count)
            classifier._accept_bytes(req, result)


@dataclass(frozen=True)
class ParsedRecord:
    event: str
    fields: Mapping[str, str]
    raw_line: str
    epoch: str
    elf_sha256: str


@dataclass(frozen=True)
class ParsedCapture:
    records: tuple[ParsedRecord, ...]
    terminal: Mapping[str, str]
    verdict: str | None
    bytes_seen: int
    read_count: int
    charged_bytes: int

    def sanitized(self) -> dict[str, object]:
        return {
            "terminal": dict(self.terminal),
            "verdict": self.verdict,
            "record_count": len(self.records),
            "read_count": self.read_count,
            "charged_bytes": self.charged_bytes,
        }


class ProtocolParser:
    """Incremental strict H37 line, binding, read-ledger, and terminal validator."""

    def __init__(self, epoch: str, elf_sha256: str, h35_reference_epoch: str,
                 expected_cid_digest: str, max_capture_bytes: int = MAX_CAPTURE_BYTES,
                 expected_geometry: Mapping[str, int] | None = None):
        if not re.fullmatch(r"[0-9a-f]{32}", epoch):
            raise ProtocolError("invalid H37 epoch")
        if not HEX64.fullmatch(elf_sha256):
            raise ProtocolError("invalid ELF SHA-256")
        if not re.fullmatch(r"[0-9a-f]{32}", h35_reference_epoch) or not HEX64.fullmatch(expected_cid_digest):
            raise ProtocolError("invalid private H35 identity binding")
        self.epoch, self.elf = epoch, elf_sha256
        self.h35_epoch, self.expected_cid_digest = h35_reference_epoch, expected_cid_digest
        self.expected_geometry = dict(expected_geometry or EXPECTED_CARD_GEOMETRY)
        self.max_capture_bytes = min(max_capture_bytes, MAX_CAPTURE_BYTES)
        self.bytes_seen = 0
        self.records: list[ParsedRecord] = []
        self.read_plan: list[tuple[int, int]] = []
        self.read_index = 0
        self.charged_bytes = 0
        self.last_seq = 0
        self.dispatched_count = 0
        self._pending_dispatch: tuple[int, int] | None = None
        self._seen_events: set[str] = set()
        self.last_read_error = 0
        self.cid_digest_matches: bool | None = None
        self.identity_matched = False
        self.ready = False
        self.cleanup: Mapping[str, str] | None = None
        self.terminal: Mapping[str, str] | None = None
        self._last_order = -1
        self._read_started = False

    def set_read_plan(self, requests: Iterable[tuple[int, int] | ReadRequest]) -> None:
        if self.terminal is not None or (self._read_started and self.read_index < len(self.read_plan)):
            raise ProtocolError("cannot extend plan before prior requests finish")
        additions = []
        for item in requests:
            lba, count = item.as_tuple() if isinstance(item, ReadRequest) else item
            if not isinstance(lba, int) or not isinstance(count, int) or lba < 0 or not 1 <= count <= MAX_SECTORS_PER_READ:
                raise ProtocolError("invalid planned read")
            additions.append((lba, count))
        projected = self.charged_bytes + sum(n * SECTOR_BYTES for _, n in additions)
        if projected > MAX_BYTES:
            raise ProtocolError("read plan exceeds metadata byte budget")
        self.read_plan.extend(additions)

    def mark_dispatched(self, lba: int, count: int) -> None:
        """Charge one stop-and-wait request immediately before sending it."""
        if self._pending_dispatch is not None:
            raise ProtocolError("previous dispatched read has no terminal READ_RESULT")
        if self.read_index >= len(self.read_plan) or self.read_plan[self.read_index] != (lba, count):
            raise ProtocolError("dispatched read differs from validated plan")
        if self.charged_bytes + count * SECTOR_BYTES > MAX_BYTES:
            raise ProtocolError("dispatched read exceeds metadata byte budget")
        self.charged_bytes += count * SECTOR_BYTES
        self.dispatched_count += 1
        self._pending_dispatch = (lba, count)

    def _parse_line(self, line: bytes | str) -> ParsedRecord | None:
        if isinstance(line, str):
            try:
                raw = line.encode("ascii", "strict")
            except UnicodeEncodeError as exc:
                raise ProtocolError("non-ASCII record") from exc
        elif isinstance(line, bytes):
            raw = line
        else:
            raise ProtocolError("record must be bytes or text")
        self.bytes_seen += len(raw)
        if self.bytes_seen > self.max_capture_bytes:
            raise ProtocolError("raw capture exceeds 1 MiB")
        if len(raw) > MAX_RECORD_BYTES:
            raise ProtocolError("record exceeds 8192 bytes")
        if not raw.endswith(b"\n"):
            raise ProtocolError("truncated record line")
        raw = raw[:-1]
        if raw.endswith(b"\r"):
            raw = raw[:-1]
        try:
            text = raw.decode("ascii", "strict")
        except UnicodeDecodeError as exc:
            raise ProtocolError("non-ASCII record") from exc
        if not text:
            raise ProtocolError("empty record")
        parts = text.split(",")
        if len(parts) < 5 or parts[0:2] != ["H37", "1"] or parts[2] not in EVENT_FIELDS:
            raise ProtocolError("unknown or malformed H37 record")
        event = parts[2]
        fields: dict[str, str] = {}
        for token in parts[3:]:
            if "=" not in token:
                raise ProtocolError("record field lacks equals sign")
            key, value = token.split("=", 1)
            if not key or not value or key in fields:
                raise ProtocolError("empty or duplicate record field")
            fields[key] = value
        if fields.get("epoch") != self.epoch or fields.get("elf_sha256") != self.elf:
            raise ProtocolError("record epoch/ELF binding mismatch")
        fields.pop("epoch")
        fields.pop("elf_sha256")
        if set(fields) != EVENT_FIELDS[event]:
            raise ProtocolError(f"unexpected or missing fields for {event}")
        return ParsedRecord(event, fields, text, self.epoch, self.elf)

    def feed_line(self, line: bytes | str) -> ParsedRecord | None:
        if self.terminal is not None:
            raise ProtocolError("record follows COMPLETE")
        rec = self._parse_line(line)
        assert rec is not None
        event, f = rec.event, rec.fields
        order = EVENT_ORDER[event]
        if event == "READ_RESULT":
            if self._last_order > EVENT_ORDER["READ_RESULT"]:
                raise ProtocolError("READ_RESULT after cleanup")
        elif order < self._last_order:
            raise ProtocolError("event order regressed")
        if event != "READ_RESULT":
            if event in self._seen_events:
                raise ProtocolError(f"duplicate event: {event}")
            self._seen_events.add(event)
        self._last_order = max(self._last_order, order)
        if event == "BOOT":
            _uint(f["reset_reason"], "reset_reason", 255)
        elif event == "TRANSPORT":
            if f != TRANSPORT_FIELDS:
                raise ProtocolError("TRANSPORT contract mismatch")
        elif event == "POWER":
            _uint(f["gpio"], "gpio", 63); _bool(f["active_level"]); _error(f["error"], "power error")
        elif event in ("HOST", "SLOT", "CARD"):
            _error(f["error"], f"{event} error")
        elif event == "GEOMETRY":
            sectors = _uint(f["sectors"], "sectors")
            sector_bytes = _uint(f["sector_bytes"], "sector_bytes", 4096)
            capacity = _uint(f["capacity_bytes"], "capacity_bytes")
            _uint(f["bus_width"], "bus_width", 8)
            _uint(f["real_freq_khz"], "real_freq_khz", 1000000)
            _bool(f["ddr"])
            if sector_bytes != SECTOR_BYTES or sectors * sector_bytes != capacity:
                raise ProtocolError("GEOMETRY inconsistent or unsupported")
            reported_geometry = {
                "sectors": sectors, "sector_bytes": sector_bytes, "capacity_bytes": capacity,
                "bus_width": int(f["bus_width"]), "real_freq_khz": int(f["real_freq_khz"]),
                "ddr": int(f["ddr"]),
            }
            if reported_geometry != self.expected_geometry:
                raise ProtocolError("GEOMETRY differs from private H35-bound expectation")
        elif event == "CID_PRIVATE":
            cid_digest = h35_cid_digest(self.h35_epoch, f)
            self.cid_digest_matches = hmac.compare_digest(cid_digest, self.expected_cid_digest)
        elif event == "READY":
            if f["accepts"] != "BIND" or self.cid_digest_matches is None:
                raise ProtocolError("READY before private CID record or invalid command")
            self.ready = True
        elif event == "IDENTITY_MATCH":
            err = _error(f["error"], "identity error")
            match = _bool(f["match"])
            if not self.ready or f["reference_epoch"] != self.h35_epoch:
                raise ProtocolError("IDENTITY_MATCH binding/order invalid")
            if match != self.cid_digest_matches or (match and err != 0) or (not match and err == 0):
                raise ProtocolError("IDENTITY_MATCH contradicts private host comparison")
            self.identity_matched = match
        elif event == "READ_RESULT":
            if not self.identity_matched:
                raise ProtocolError("READ_RESULT before identity match")
            seq = _uint(f["seq"], "sequence", 0xFFFFFFFF)
            lba = _uint(f["lba"], "LBA")
            count = _uint(f["count"], "count", MAX_SECTORS_PER_READ)
            charged = _uint(f["charged_total"], "charged_total", MAX_BYTES)
            error = _error(f["error"], "read error")
            _uint(f["elapsed_us"], "elapsed_us")
            length = _uint(f["len"], "len", MAX_SECTORS_PER_READ * SECTOR_BYTES)
            if count == 0 or seq != self.last_seq + 1 or self.read_index >= len(self.read_plan) or self._pending_dispatch is None:
                raise ProtocolError("unexpected read sequence")
            expected_lba, expected_count = self.read_plan[self.read_index]
            if (lba, count) != (expected_lba, expected_count) or self._pending_dispatch != (lba, count):
                raise ProtocolError("read result differs from validated plan")
            self._read_started = True
            self.read_index += 1
            self.last_seq = seq
            self._pending_dispatch = None
            if self.charged_bytes > MAX_BYTES or charged != self.charged_bytes:
                raise ProtocolError("read charge mismatch or cap exceeded")
            if error == 0:
                if length != count * SECTOR_BYTES or not HEX64.fullmatch(f["sha256"]):
                    raise ProtocolError("successful READ_RESULT length/hash invalid")
                try:
                    payload = base64.b64decode(f["b64"], validate=True)
                except (binascii.Error, ValueError) as exc:
                    raise ProtocolError("READ_RESULT base64 invalid") from exc
                if len(payload) != length or hashlib.sha256(payload).hexdigest() != f["sha256"]:
                    raise ProtocolError("READ_RESULT payload length/hash mismatch")
            else:
                if length != 0 or f["sha256"] != "none" or f["b64"] != "none":
                    raise ProtocolError("errored READ_RESULT must contain no payload")
            self.last_read_error = error
        elif event == "CLEANUP":
            attempts = (_bool(f["host_deinit_attempted"]), _bool(f["power_off_attempted"]))
            deinit_error = _error(f["deinit_error"], "deinit_error")
            power_error = _error(f["power_off_error"], "power_off_error")
            # The firmware initializes the host and power GPIO in separate
            # stages. Cleanup is required only for resources that were
            # acquired. A failed HOST init must not be deinitialized; a
            # successful one must be. POWER.error alone cannot distinguish a
            # failed gpio_config (not configured) from a failed initial
            # gpio_set_level after gpio_config succeeded (configured). In
            # that one case either cleanup-attempt value is structurally
            # possible under the frozen POWER event; see the source-gate note
            # and fixtures for this ambiguity.
            host_record = next((r for r in self.records if r.event == "HOST"), None)
            power_record = next((r for r in self.records if r.event == "POWER"), None)
            host_error = None if host_record is None else _error(host_record.fields["error"], "HOST error")
            power_init_error = None if power_record is None else _error(power_record.fields["error"], "POWER error")
            host_required = host_error == 0
            if attempts[0] != host_required or (not attempts[0] and deinit_error != 0):
                raise ProtocolError("host cleanup attempt does not match host acquisition")
            if power_init_error is None:
                power_required = False
            elif power_init_error == 0:
                power_required = True
            else:
                power_required = None  # config/set-level failure is ambiguous in POWER v1
            if power_required is True and not attempts[1]:
                raise ProtocolError("power cleanup omitted after successful setup")
            if power_required is False and attempts[1]:
                raise ProtocolError("power cleanup attempted before power setup")
            if not attempts[1] and power_error != 0:
                raise ProtocolError("unattempted power cleanup has an error")
            self.cleanup = dict(f)
        elif event == "COMPLETE":
            self._validate_terminal(f)
            self.terminal = dict(f)
        self.records.append(rec)
        return rec

    def _validate_terminal(self, f: Mapping[str, str]) -> None:
        if self.cleanup is None:
            raise ProtocolError("COMPLETE before CLEANUP")
        result = f["result"]
        stage = f["failure_stage"]
        if result not in ("read_complete", "failed") or stage not in FAILURE_STAGES:
            raise ProtocolError("invalid terminal enumeration")
        error = _error(f["error"], "terminal error")
        count = _uint(f["read_count"], "read_count", 256)
        charged = _uint(f["charged_total"], "terminal charged_total", MAX_BYTES)
        bound = _bool(f["bound"])
        writes = _bool(f["media_writes"])
        if f["scope"] != "classification_only" or writes:
            raise ProtocolError("invalid terminal scope/write flag")
        if count != self.dispatched_count or charged != self.charged_bytes:
            raise ProtocolError("terminal counts differ from ledger")
        cleanup_errors = (_error(self.cleanup["deinit_error"], "deinit_error"),
                          _error(self.cleanup["power_off_error"], "power_off_error"))
        if result == "read_complete":
            if stage != "none" or error != 0 or not bound or not self.identity_matched:
                raise ProtocolError("invalid successful terminal state")
            required = ["BOOT", "TRANSPORT", "POWER", "HOST", "SLOT", "CARD", "GEOMETRY",
                        "CID_PRIVATE", "READY", "IDENTITY_MATCH", "CLEANUP", "COMPLETE"]
            names = [r.event for r in self.records]
            if names[:10] != required[:10] or not names or names[-1] != "CLEANUP":
                raise ProtocolError("successful transcript event sequence incomplete")
            first_read = names.index("READ_RESULT") if "READ_RESULT" in names else len(names)
            if names[10:first_read] or names[first_read + self.last_seq:-1]:
                raise ProtocolError("successful transcript event ordering invalid")
            if self._pending_dispatch is not None or self.read_index != len(self.read_plan) or self.last_read_error:
                raise ProtocolError("successful terminal has incomplete/failed reads")
            for event in ("POWER", "HOST", "SLOT", "CARD"):
                if int(self._record_fields(event)["error"]) != 0:
                    raise ProtocolError(f"successful transcript has {event} error")
            if cleanup_errors != (0, 0):
                raise ProtocolError("successful terminal has cleanup errors")
        else:
            if stage == "none" or error == 0:
                raise ProtocolError("failed terminal needs a nonzero stage-matched error")
            stage_event = {"power": "POWER", "host": "HOST", "slot": "SLOT",
                           "card": "CARD", "bind": "IDENTITY_MATCH"}.get(stage)
            if stage_event is not None:
                event_fields = self._record_fields(stage_event)
                error_field = "error"
                if _error(event_fields[error_field], f"{stage_event} error") != error:
                    raise ProtocolError("failed terminal error differs from its failure-stage event")
            if stage == "read" and self.last_read_error != error:
                raise ProtocolError("read terminal error differs from READ_RESULT")
            if stage == "cleanup":
                cleanup_error = cleanup_errors[1] if cleanup_errors[1] != 0 else cleanup_errors[0]
                if error != cleanup_error:
                    raise ProtocolError("cleanup terminal error differs from CLEANUP")
            if stage != "cleanup" and any(cleanup_errors):
                raise ProtocolError("cleanup failure must be the terminal failure stage")
            # A nonzero stage record cannot be relabeled as an unrelated
            # failure (for example, POWER.error followed by COMPLETE(timeout)).
            # A later cleanup failure is the sole allowed override because
            # firmware's finish() gives cleanup errors precedence.
            for event, event_stage in (("POWER", "power"), ("HOST", "host"),
                                       ("SLOT", "slot"), ("CARD", "card"),
                                       ("IDENTITY_MATCH", "bind")):
                try:
                    event_error = _error(self._record_fields(event)["error"], f"{event} error")
                except ProtocolError:
                    continue
                if event_error != 0 and stage != "cleanup":
                    if stage != event_stage or error != event_error:
                        raise ProtocolError(f"terminal stage/error does not match failed {event} event")
            if self._pending_dispatch is not None and stage not in ("timeout", "command", "cleanup"):
                raise ProtocolError("unanswered dispatched request has wrong failure stage")
            if bound and not self.identity_matched:
                raise ProtocolError("failed terminal claims unmatched identity bound")

    def _record_fields(self, event: str) -> Mapping[str, str]:
        for record in self.records:
            if record.event == event:
                return record.fields
        raise ProtocolError(f"missing {event} record")

    def finish(self) -> ParsedCapture:
        if self.terminal is None:
            raise ProtocolError("capture has no final COMPLETE")
        if not self.records or self.records[-1].event != "COMPLETE":
            raise ProtocolError("COMPLETE is not final record")
        success = self.terminal["result"] == "read_complete"
        verdict = None
        if success:
            # Metadata verdict is added only after controller runs classifier on private bytes.
            verdict = "read_complete"
        return ParsedCapture(tuple(self.records), self.terminal, verdict,
                             self.bytes_seen, self.dispatched_count, self.charged_bytes)


def _bool(value: str) -> bool:
    if value not in ("0", "1"):
        raise ProtocolError("boolean must be literal 0 or 1")
    return value == "1"


def extract_h37_record_lines(raw_capture: bytes) -> tuple[bytes, ...]:
    """Select complete-line H37 records without searching/substrings.

    ROM/native USB startup lines and echoed `H37C,` host commands remain in the
    caller's separate raw capture. Any other line beginning with `H37` but not
    the exact record prefix is rejected as a malformed H37 record candidate.
    """
    if not isinstance(raw_capture, bytes) or len(raw_capture) > MAX_CAPTURE_BYTES:
        raise ProtocolError("raw capture must be bytes within 1 MiB")
    out = []
    for line in raw_capture.splitlines(keepends=True):
        if line.startswith(b"H37,"):
            out.append(line)
        elif line.startswith(b"H37C,"):
            continue
        elif line.startswith(b"H37"):
            raise ProtocolError("malformed H37 line prefix")
    return tuple(out)


def parse_capture(raw: bytes | str, epoch: str, elf_sha256: str,
                  h35_reference_epoch: str, expected_cid_digest: str,
                  read_plan: Iterable[tuple[int, int] | ReadRequest],
                  dispatched_reads: Iterable[tuple[int, int]] | None = None,
                  expected_geometry: Mapping[str, int] | None = None) -> ParsedCapture:
    """Strictly validate a bounded complete transcript against its read plan."""
    data = raw.encode("ascii", "strict") if isinstance(raw, str) else raw
    if not isinstance(data, bytes) or len(data) > MAX_CAPTURE_BYTES:
        raise ProtocolError("capture must be bytes within 1 MiB")
    record_lines = extract_h37_record_lines(data)
    parser = ProtocolParser(epoch, elf_sha256, h35_reference_epoch, expected_cid_digest,
                            expected_geometry=expected_geometry)
    plans = [r.as_tuple() if isinstance(r, ReadRequest) else tuple(r) for r in read_plan]
    parser.set_read_plan(plans)
    dispatches = list(dispatched_reads) if dispatched_reads is not None else None
    dispatch_index = 0
    for line in record_lines:
        parts = line.split(b",", 3)
        event = parts[2].decode("ascii", "ignore") if len(parts) > 2 else ""
        if event == "READ_RESULT":
            if dispatches is None:
                if dispatch_index >= len(plans):
                    raise ProtocolError("READ_RESULT has no planned dispatch")
                request = plans[dispatch_index]
            else:
                if dispatch_index >= len(dispatches):
                    raise ProtocolError("READ_RESULT has no matching host dispatch")
                request = dispatches[dispatch_index]
            parser.mark_dispatched(*request)
            dispatch_index += 1
        elif event == "COMPLETE" and dispatches is not None:
            remaining = len(dispatches) - dispatch_index
            if remaining > 1:
                raise ProtocolError("stop-and-wait ledger has multiple unanswered dispatches")
            if remaining == 1:
                parser.mark_dispatched(*dispatches[dispatch_index])
                dispatch_index += 1
        parser.feed_line(line)
    if dispatches is not None and dispatch_index != len(dispatches):
        raise ProtocolError("host dispatch ledger has entries beyond COMPLETE")
    parsed = parser.finish()
    return ParsedCapture(parsed.records, parsed.terminal, parsed.verdict,
                         len(data), parsed.read_count, parsed.charged_bytes)


def sanitized_summary(result: ClassificationResult, capture: ParsedCapture) -> dict[str, object]:
    """Combine classifier and transcript summaries under an explicit field allowlist."""
    if capture.terminal.get("result") != "read_complete":
        raise ProtocolError("cannot publish a classifier verdict for failed capture")
    return {
        "epoch": capture.records[0].epoch,
        "verdict": result.verdict,
        "scheme": result.scheme,
        "signatures": list(result.signatures),
        "partitions": result.sanitized()["partitions"],
        "read_count": capture.read_count,
        "read_bytes": capture.charged_bytes,
        "terminal": "read_complete",
    }
