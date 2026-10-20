#!/usr/bin/env python3
"""Strict parser for bounded LAB-04 serial traces."""
from __future__ import annotations

import argparse
import json
import re
import math
from pathlib import Path
from typing import Any, Mapping

PARSER_DOCUMENT = "family-link.lab-metrics/v2"
TRACE_SCHEMA = "lab04-trace/v1"
PARSER_VERSION = "lab04-parser/v1"
MAX_INPUT_BYTES = 4 * 1024 * 1024
MAX_OUTPUT_BYTES = 8 * 1024 * 1024
MAX_LINES = 20_000
MAX_LINE_BYTES = 4096
MAX_COUNTER = 2**32 - 1
_MAX_BY_FIELD = {"operation_id": 2**64 - 1, "monotonic_us": 2**63 - 1}
DECIDED = {"F-malformed": "failure", "F-overflow": "failure", "F-cancel": "cancelled"}
DECIDED_INVENTORY = {
    "F-malformed": {"points": 3, "strokes": 3, "preview": 3},
    "F-overflow": {"points": 1, "strokes": 1, "preview": 1},
    "F-cancel": {"points": 1, "strokes": 1, "preview": 1},
}
DECIDED_REASONS = {
    "F-malformed": {
        "points": {"truncated_records", "input_too_large", "wrong_color_count"},
        "strokes": {"truncated_stroke", "input_too_large", "wrong_color_count"},
        "preview": {"truncated_records", "input_too_large", "wrong_color_count"},
    },
    "F-overflow": {name: {"overflow"} for name in ("points", "strokes", "preview")},
    "F-cancel": {name: {"cancelled_mid_decode"} for name in ("points", "strokes", "preview")},
}
EXPLORATORY = {"F-points", "F-strokes", "F-preview", "F-checkpoint", "F-legacy"}
_FIXTURES = {"corpus-a-short", "corpus-a-dense", "corpus-b"}
_VARIANTS = {"points", "strokes", "preview", "legacy", "legacy-colored", "unsupported"}
_TRACE_FIELDS = {"schema", "event", "seq", "monotonic_us", "scenario", "fixture", "class", "variant", "checkpoint_position", "operation_id", "terminal", "terminal_count", "reason", "encoded_bytes", "preview_bytes", "preview_buffers", "live_buffers", "position_ms", "encode_us", "decode_us", "raster_us", "raster_work_cap", "raster_eligible", "raster_rendered", "raster_remaining", "seek_us", "internal_largest_free_block_bytes", "psram_largest_free_block_bytes", "lvgl_largest_free_block_bytes"}
_RESOURCE_FIELDS = {
    "schema", "event", "seq", "monotonic_us", "scenario", "fixture", "variant", "operation_id",
    "sample_kind", "phase", "image_resident", "image_storage", "image_bytes", "decoded_resident",
    "decoded_segments", "ledger_b_image_resident", "ledger_b_image_storage", "ledger_b_image_bytes",
    "ledger_b_image_caps", "ledger_b_image_lifetime", "ledger_b_decoded_resident",
    "ledger_b_decoded_segments", "ledger_b_decoded_capacity_bytes", "internal_largest_free_block_bytes",
    "psram_largest_free_block_bytes", "lvgl_total_size_bytes", "lvgl_free_size_bytes", "lvgl_used_pct",
    "lvgl_free_biggest_size_bytes", "lvgl_largest_free_block_bytes", "lvgl_max_used_bytes", "lvgl_sample_status",
}
_PEAK_FIELDS = {
    "schema", "event", "seq", "monotonic_us", "scenario", "fixture", "variant", "operation_id",
    "sample_kind", "phase", "image_resident", "image_storage", "image_bytes", "decoded_resident",
    "expected_output_segments", "decoded_capacity_bytes", "internal_largest_free_block_bytes",
    "psram_largest_free_block_bytes", "lvgl_total_size_bytes", "lvgl_free_size_bytes", "lvgl_used_pct",
    "lvgl_free_biggest_size_bytes", "lvgl_largest_free_block_bytes", "lvgl_max_used_bytes",
    "lvgl_sample_status", "ledger_b_image_resident", "ledger_b_image_storage", "ledger_b_image_bytes",
    "ledger_b_image_caps", "ledger_b_image_lifetime", "ledger_b_decoded_resident",
    "ledger_b_expected_output_segments", "ledger_b_decoded_capacity_bytes",
}
# Empty values are required for checkpoint_position on non-seek trace rows.
_KV = re.compile(r"([a-z][a-z0-9_]*)=([^\s]*)")
_RUN_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$")
_RAW_SECRET = re.compile(
    r"(?i)(?:\bpin\s*[=:]\s*\d{4,}|\b(?:password|passwd)\s*[=:]\s*\S+|"
    r"\bbearer\s+[A-Za-z0-9._~+/=-]{8,}|\b(?:media_payload|payload_b64|image_data|audio_data)\s*[=:])"
)
_EVIDENCE_BLOCKED = re.compile(
    r"(?i)(?:\bpin\s*[=:]\s*\S+|\b(?:media_payload|payload_b64|image_data|audio_data)\s*[=:])"
)
_SERIES_FIELDS = {
    "encode_us": ("encode_us", "us"),
    "decode_us": ("decode_us", "us"),
    "raster_us": ("raster_us", "us"),
    "seek_us": ("seek_us", "us"),
    "internal_largest_free_block_bytes": ("internal_largest_free_block_bytes", "bytes"),
    "psram_largest_free_block_bytes": ("psram_largest_free_block_bytes", "bytes"),
    "lvgl_largest_free_block_bytes": ("lvgl_largest_free_block_bytes", "bytes"),
}
_ENCODING_SCENARIOS = {"F-points", "F-strokes", "F-preview"}
_PERCENTILES = (("p50", 50), ("p95", 95), ("p99", 99))


class ParseError(ValueError):
    """Input cannot be trusted as complete LAB-04 evidence."""


def redact_line(line: str) -> str:
    """Redact URLs, password assignments, and bearer credentials."""
    line = re.sub(r"(?i)https?://[^\s\"']+", "[REDACTED_URL]", line)
    line = re.sub(r"(?i)(password|passwd)\s*[=:]\s*[^\s,;]+", r"\1=[REDACTED]", line)
    line = re.sub(r"(?i)Bearer\s+[A-Za-z0-9._~+/=-]+", "Bearer [REDACTED]", line)
    return line


def validate_evidence_line(line: str) -> None:
    """Reject sensitive fields before a serial line can enter run evidence."""
    if _EVIDENCE_BLOCKED.search(line):
        raise ParseError("serial line contains a raw PIN or media payload")


def _fields(line: str) -> dict[str, str]:
    body = line[len("LAB04_TRACE "):].strip()
    matches = list(_KV.finditer(body))
    if not matches or " ".join(m.group(0) for m in matches) != body:
        raise ParseError("malformed trace key/value row")
    result: dict[str, str] = {}
    for match in matches:
        key, value = match.groups()
        if key in result:
            raise ParseError(f"duplicate trace field: {key}")
        result[key] = value
    return result


def _marker_fields(line: str, prefix: str) -> dict[str, str]:
    body = line[len(prefix):]
    matches = list(_KV.finditer(body))
    if not matches or " ".join(m.group(0) for m in matches) != body.strip():
        raise ParseError(f"malformed {prefix.strip()} marker")
    values: dict[str, str] = {}
    for match in matches:
        key, value = match.groups()
        if key in values:
            raise ParseError(f"duplicate {prefix.strip()} marker field")
        values[key] = value
    return values


def _uint(row: dict[str, str], key: str, *, required: bool = False) -> int | None:
    value = row.get(key)
    if value is None:
        if required:
            raise ParseError(f"missing {key}")
        return None
    if not re.fullmatch(r"0|[1-9][0-9]*", value):
        raise ParseError(f"invalid integer {key}")
    parsed = int(value)
    if parsed > _MAX_BY_FIELD.get(key, MAX_COUNTER):
        raise ParseError(f"integer overflow {key}")
    return parsed


def _trace_rows(serial: str) -> tuple[list[dict[str, Any]], dict[str, Any] | None,
                                      dict[str, Any] | None, list[dict[str, Any]],
                                      list[dict[str, Any]], dict[str, Any]]:
    try:
        encoded = serial.encode("utf-8", errors="strict")
    except UnicodeEncodeError as exc:
        raise ParseError("serial is not valid UTF-8 text") from exc
    if len(encoded) > MAX_INPUT_BYTES:
        raise ParseError("serial input exceeds byte limit")
    lines = serial.splitlines()
    if len(lines) > MAX_LINES:
        raise ParseError("serial input exceeds line limit")
    rows: list[dict[str, Any]] = []
    firmware_loss: dict[str, Any] | None = None
    publication_loss: dict[str, Any] | None = None
    resource_samples: list[dict[str, Any]] = []
    resource_peaks: list[dict[str, Any]] = []
    summary: dict[str, Any] = {"status": "UNKNOWN", "expected_rows": None,
                               "publication_queue_drops": None, "serial_write_errors": None}
    last_sequenced_line = -1
    publication_line = -1
    summary_line = -1
    harness_line = next((i for i, line in enumerate(lines) if line.startswith("LAB04_HARNESS_DONE ")), -1)
    seqs: set[int] = set()
    operations: set[int] = set()
    for line_index, line in enumerate(lines):
        if len(line.encode("utf-8")) > MAX_LINE_BYTES:
            raise ParseError("serial line exceeds byte limit")
        if _RAW_SECRET.search(line):
            raise ParseError("serial fixture contains secret or raw media payload")
        if not line.startswith("LAB04_TRACE "):
            continue
        try:
            fields = _fields(line)
        except ParseError:
            if "event=trace_summary" in line:
                summary = {"status": "MALFORMED", "expected_rows": None,
                           "publication_queue_drops": None, "serial_write_errors": None}
                continue
            raise
        if fields.get("schema") != TRACE_SCHEMA:
            raise ParseError("wrong trace schema")
        if fields.get("event") == "trace_loss":
            if firmware_loss is not None:
                raise ParseError("duplicate firmware trace loss row")
            expected = {"schema", "event", "dropped_events", "oversized_events", "malformed_rows"}
            if fields.keys() != expected:
                raise ParseError("malformed firmware trace loss row")
            loss_values = {}
            for key in ("dropped_events", "oversized_events", "malformed_rows"):
                loss_values[key] = _uint(fields, key, required=True)
            firmware_loss = {"status": "OBSERVED", **loss_values}
            continue
        if fields.get("event") == "publication_loss":
            if publication_loss is not None:
                raise ParseError("duplicate firmware publication loss row")
            expected = {"schema", "event", "source", "dropped_events"}
            if fields.keys() != expected or fields.get("source") != "firmware_publication_queue":
                raise ParseError("malformed firmware publication loss row")
            publication_loss = {"status": "OBSERVED", "source": fields["source"],
                                "dropped_events": _uint(fields, "dropped_events", required=True)}
            publication_line = line_index
            continue
        if fields.get("event") == "trace_summary":
            summary_line = line_index
            expected = {"schema", "event", "expected_rows", "publication_queue_drops", "serial_write_errors"}
            if summary["status"] != "UNKNOWN":
                summary = {"status": "MALFORMED", "expected_rows": None,
                           "publication_queue_drops": None, "serial_write_errors": None}
                continue
            if fields.keys() != expected or fields.get("schema") != TRACE_SCHEMA:
                summary = {"status": "MALFORMED", "expected_rows": None,
                           "publication_queue_drops": None, "serial_write_errors": None}
                continue
            try:
                summary = {"status": "OBSERVED",
                           "expected_rows": _uint(fields, "expected_rows", required=True),
                           "publication_queue_drops": _uint(fields, "publication_queue_drops", required=True),
                           "serial_write_errors": _uint(fields, "serial_write_errors", required=True)}
            except ParseError:
                summary = {"status": "MALFORMED", "expected_rows": None,
                           "publication_queue_drops": None, "serial_write_errors": None}
            continue
        if fields.get("event") == "resource_sample":
            unknown = fields.keys() - _RESOURCE_FIELDS
            required = {"schema", "event", "seq", "monotonic_us", "scenario", "fixture", "variant",
                        "operation_id", "sample_kind", "phase", "image_resident", "image_storage",
                        "image_bytes", "decoded_resident", "decoded_segments", "ledger_b_image_resident",
                        "ledger_b_image_storage", "ledger_b_image_bytes", "ledger_b_image_caps",
                        "ledger_b_image_lifetime", "ledger_b_decoded_resident", "ledger_b_decoded_segments",
                        "ledger_b_decoded_capacity_bytes", "internal_largest_free_block_bytes",
                        "psram_largest_free_block_bytes"}
            if unknown or required - fields.keys():
                raise ParseError("malformed resource sample fields")
            if len(resource_samples) >= 128:
                raise ParseError("resource sample count exceeds bound")
            sample: dict[str, Any] = dict(fields)
            numeric = ("seq", "monotonic_us", "operation_id", "image_resident", "image_bytes",
                       "decoded_resident", "decoded_segments", "ledger_b_image_resident",
                       "ledger_b_image_bytes", "ledger_b_decoded_resident", "ledger_b_decoded_segments",
                       "ledger_b_decoded_capacity_bytes", "internal_largest_free_block_bytes",
                       "psram_largest_free_block_bytes", "lvgl_total_size_bytes", "lvgl_free_size_bytes",
                       "lvgl_used_pct", "lvgl_free_biggest_size_bytes", "lvgl_largest_free_block_bytes",
                       "lvgl_max_used_bytes")
            for key in numeric:
                value = _uint(fields, key, required=key in required)
                if value is not None:
                    sample[key] = value
            if sample["seq"] < 1 or sample["operation_id"] < 1:
                raise ParseError("invalid resource sample identity")
            if sample["seq"] in seqs:
                raise ParseError("duplicate trace sequence")
            seqs.add(sample["seq"])
            if (sample["scenario"] != "F-preview" or sample["fixture"] not in {"corpus-a-short", "corpus-b"}
                    or sample["variant"] != "preview" or sample["sample_kind"] != "point"
                    or sample["phase"] != "post_decode_pre_raster"):
                raise ParseError("resource sample scenario or phase mismatch")
            constants = {"image_resident": 1, "image_storage": "encoded_payload", "image_bytes": 28800,
                         "decoded_resident": 1, "ledger_b_image_resident": 1,
                         "ledger_b_image_storage": "encoded_payload", "ledger_b_image_bytes": 28800,
                         "ledger_b_image_caps": "MALLOC_CAP_DEFAULT",
                         "ledger_b_image_lifetime": "encoded_result_live_through_raster",
                         "ledger_b_decoded_resident": 1}
            if any(sample.get(key) != expected for key, expected in constants.items()):
                raise ParseError("resource sample violates declared Ledger B contract")
            lvgl_complete = {"lvgl_total_size_bytes", "lvgl_free_size_bytes", "lvgl_used_pct",
                             "lvgl_free_biggest_size_bytes", "lvgl_largest_free_block_bytes", "lvgl_max_used_bytes"}
            lvgl_values = lvgl_complete & sample.keys()
            if lvgl_values and lvgl_values != lvgl_complete:
                raise ParseError("incomplete LVGL resource ledger")
            if lvgl_values and sample.get("lvgl_sample_status") is not None:
                raise ParseError("LVGL sample cannot be both measured and unavailable")
            if not lvgl_values and sample.get("lvgl_sample_status") != "display_lock_busy":
                raise ParseError("missing LVGL resource ledger status")
            if (sample.get("lvgl_used_pct", 0) > 100
                    or sample.get("lvgl_free_size_bytes", 0) > sample.get("lvgl_total_size_bytes", 0)
                    or sample["decoded_segments"] < 1 or sample["ledger_b_decoded_capacity_bytes"] < 1):
                raise ParseError("resource sample contains an out-of-range ledger value")
            resource_samples.append(sample)
            last_sequenced_line = line_index
            continue
        if fields.get("event") == "resource_peak":
            required = {"schema", "event", "seq", "monotonic_us", "scenario", "fixture", "variant",
                        "operation_id", "sample_kind", "phase", "image_resident", "image_storage",
                        "image_bytes", "decoded_resident", "expected_output_segments", "decoded_capacity_bytes",
                        "internal_largest_free_block_bytes", "psram_largest_free_block_bytes",
                        "ledger_b_image_resident", "ledger_b_image_storage", "ledger_b_image_bytes",
                        "ledger_b_image_caps", "ledger_b_image_lifetime", "ledger_b_decoded_resident",
                        "ledger_b_expected_output_segments", "ledger_b_decoded_capacity_bytes"}
            unknown = fields.keys() - _PEAK_FIELDS
            if unknown or required - fields.keys():
                raise ParseError("malformed resource peak fields")
            if len(resource_peaks) >= 128:
                raise ParseError("resource peak count exceeds bound")
            peak: dict[str, Any] = dict(fields)
            numeric = ("seq", "monotonic_us", "operation_id", "image_resident", "image_bytes",
                       "decoded_resident", "expected_output_segments", "decoded_capacity_bytes",
                       "internal_largest_free_block_bytes", "psram_largest_free_block_bytes",
                       "ledger_b_image_resident", "ledger_b_image_bytes", "ledger_b_decoded_resident",
                       "ledger_b_expected_output_segments", "ledger_b_decoded_capacity_bytes",
                       "lvgl_total_size_bytes", "lvgl_free_size_bytes", "lvgl_used_pct",
                       "lvgl_free_biggest_size_bytes", "lvgl_largest_free_block_bytes", "lvgl_max_used_bytes")
            for key in numeric:
                value = _uint(fields, key, required=key in required)
                if value is not None:
                    peak[key] = value
            if peak["seq"] < 1 or peak["operation_id"] < 1 or peak["expected_output_segments"] < 1:
                raise ParseError("invalid resource peak identity or reservation")
            if peak["seq"] in seqs:
                raise ParseError("duplicate trace sequence")
            seqs.add(peak["seq"])
            constants = {"scenario": "F-preview", "fixture": peak["fixture"], "variant": "preview",
                         "sample_kind": "peak", "phase": "maximum_output_capacity_reserved_before_record_insertion_encoded_payload_live",
                         "image_resident": 1, "image_storage": "encoded_payload", "image_bytes": 28800,
                         "decoded_resident": 1, "ledger_b_image_resident": 1,
                         "ledger_b_image_storage": "encoded_payload", "ledger_b_image_bytes": 28800,
                         "ledger_b_image_caps": "MALLOC_CAP_DEFAULT",
                         "ledger_b_image_lifetime": "encoded_result_live_through_raster",
                         "ledger_b_decoded_resident": 1,
                         "ledger_b_expected_output_segments": peak["expected_output_segments"],
                         "ledger_b_decoded_capacity_bytes": peak["decoded_capacity_bytes"]}
            if (peak["fixture"] not in {"corpus-a-short", "corpus-b"}
                    or any(peak.get(key) != value for key, value in constants.items())
                    or peak["expected_output_segments"] != peak["ledger_b_expected_output_segments"]
                    or peak["decoded_capacity_bytes"] != peak["ledger_b_decoded_capacity_bytes"]):
                raise ParseError("resource peak violates decoder reservation contract")
            lvgl_complete = {"lvgl_total_size_bytes", "lvgl_free_size_bytes", "lvgl_used_pct",
                             "lvgl_free_biggest_size_bytes", "lvgl_largest_free_block_bytes", "lvgl_max_used_bytes"}
            lvgl_values = lvgl_complete & peak.keys()
            if (lvgl_values and (lvgl_values != lvgl_complete or peak.get("lvgl_sample_status") is not None)
                    or not lvgl_values and peak.get("lvgl_sample_status") != "display_lock_busy"):
                raise ParseError("resource peak has incomplete LVGL ledger")
            if (peak.get("lvgl_used_pct", 0) > 100
                    or peak.get("lvgl_free_size_bytes", 0) > peak.get("lvgl_total_size_bytes", 0)):
                raise ParseError("resource peak contains an out-of-range LVGL ledger")
            resource_peaks.append(peak)
            last_sequenced_line = line_index
            continue
        if fields.get("event") != "scenario_terminal":
            continue
        unknown = fields.keys() - _TRACE_FIELDS
        if unknown:
            raise ParseError(f"unknown trace fields: {','.join(sorted(unknown))}")
        required = {"seq", "scenario", "fixture", "class", "variant", "operation_id", "terminal", "terminal_count"}
        missing = required - fields.keys()
        if missing:
            raise ParseError(f"missing trace fields: {','.join(sorted(missing))}")
        raster_fields = {"raster_work_cap", "raster_eligible", "raster_rendered", "raster_remaining"}
        present_raster_fields = raster_fields & fields.keys()
        if present_raster_fields and present_raster_fields != raster_fields:
            raise ParseError("incomplete bounded raster accounting fields")
        row: dict[str, Any] = dict(fields)
        for key in ("seq", "monotonic_us", "operation_id", "terminal_count", "encoded_bytes", "preview_bytes", "preview_buffers", "live_buffers", "position_ms", "encode_us", "decode_us", "raster_us", "raster_work_cap", "raster_eligible", "raster_rendered", "raster_remaining", "seek_us", "internal_largest_free_block_bytes", "psram_largest_free_block_bytes", "lvgl_largest_free_block_bytes"):
            value = _uint(fields, key, required=key in {"seq", "operation_id", "terminal_count"})
            if value is not None:
                row[key] = value
        if row["seq"] in seqs:
            raise ParseError("duplicate trace sequence")
        if row["operation_id"] in operations:
            raise ParseError("duplicate operation terminal")
        seqs.add(row["seq"])
        operations.add(row["operation_id"])
        if row["seq"] < 1 or row["operation_id"] < 1 or row["terminal_count"] != 1:
            raise ParseError("invalid sequence, operation, or terminal count")
        if present_raster_fields and (row["raster_work_cap"] != 64 or row["raster_rendered"] > row["raster_work_cap"]
                or row["raster_rendered"] > row["raster_eligible"]
                or row["raster_remaining"] != row["raster_eligible"] - row["raster_rendered"]):
            raise ParseError("invalid bounded raster accounting")
        if row["scenario"] not in set(DECIDED) | EXPLORATORY:
            raise ParseError("unknown scenario")
        if row["fixture"] not in _FIXTURES or row["variant"] not in _VARIANTS:
            raise ParseError("unknown fixture or encoding variant")
        if row.get("checkpoint_position", "") not in {"", "early", "middle", "late"}:
            raise ParseError("unknown checkpoint position")
        if len(row.get("reason", "")) > 64:
            raise ParseError("trace reason exceeds bound")
        expected_class = "decided" if row["scenario"] in DECIDED else "exploratory"
        if row["class"] != expected_class:
            raise ParseError("scenario class mismatch")
        if row["terminal"] not in {"success", "failure", "cancelled", "unsupported"}:
            raise ParseError("unknown terminal")
        if row["scenario"] in {"F-points", "F-strokes", "F-preview"}:
            if "encoded_bytes" not in row:
                raise ParseError("missing encoded_bytes for encoding observation")
        if row.get("preview_bytes", 0) > 28800 or row.get("preview_buffers", 0) > 1:
            raise ParseError("preview storage exceeds declared bound")
        rows.append(row)
        last_sequenced_line = line_index
    if not rows:
        raise ParseError("missing LAB04_TRACE scenario rows")
    operation_rows = {row["operation_id"]: row for row in rows}
    sample_operations: set[int] = set()
    for sample in resource_samples:
        operation_id = sample["operation_id"]
        terminal = operation_rows.get(operation_id)
        if (operation_id in sample_operations or terminal is None or terminal["scenario"] != "F-preview"
                or terminal["variant"] != "preview" or terminal["fixture"] != sample["fixture"]
                or terminal["terminal"] != "success"):
            raise ParseError("resource sample does not match one successful preview operation")
        sample_operations.add(operation_id)
    peak_operations: set[int] = set()
    for peak in resource_peaks:
        operation_id = peak["operation_id"]
        terminal = operation_rows.get(operation_id)
        if operation_id in peak_operations:
            raise ParseError("duplicate resource peak for operation")
        if (terminal is None or terminal["scenario"] != "F-preview" or terminal["variant"] != "preview"
                or terminal["fixture"] != peak["fixture"] or terminal["terminal"] != "success"):
            raise ParseError("resource peak does not match one successful preview operation")
        peak_operations.add(operation_id)
    all_seqs = sorted(seqs)
    reconciliation = {"status": "UNKNOWN", "observed_sequenced_rows": len(all_seqs),
                      "missing_sequences": [], "duplicate_sequences": [], "out_of_range_sequences": [],
                      "row_count_matches_summary": None}
    if summary["status"] == "OBSERVED":
        expected = summary["expected_rows"]
        missing = sorted(set(range(1, expected + 1)) - seqs) if expected <= MAX_LINES else []
        out_of_range = sorted(seq for seq in seqs if seq > expected)
        footer_order_valid = (publication_line > last_sequenced_line >= 0 and summary_line > publication_line
                              and (harness_line < 0 or summary_line < harness_line))
        reconciliation.update({"status": "COMPLETE" if expected <= MAX_LINES and footer_order_valid
                               and not missing and not out_of_range and len(seqs) == expected else "INCOMPLETE",
                               "missing_sequences": missing, "out_of_range_sequences": out_of_range,
                               "row_count_matches_summary": len(seqs) == expected,
                               "footer_order_valid": footer_order_valid,
                               "expected_rows_within_parser_line_limit": expected <= MAX_LINES})
    return rows, firmware_loss, publication_loss, resource_samples, resource_peaks, {"summary": summary, "reconciliation": reconciliation}


def _markers(serial: str, mode: str, expected_build_id: str | None = None) -> tuple[dict[str, Any], bool]:
    ready = [line for line in serial.splitlines() if line.startswith("LAB04_READY ")]
    harness = [line for line in serial.splitlines() if line.startswith("LAB04_HARNESS_DONE ")]
    if len(ready) != 1:
        raise ParseError("missing or duplicate LAB04_READY marker")
    ready_fields = _marker_fields(ready[0], "LAB04_READY ")
    expected_build = expected_build_id or ("lab04-fw-v1-scripted" if mode == "scripted" else "lab04-fw-v1-manual")
    if ready_fields.get("build") != expected_build or ready_fields.get("mode") != mode or ready_fields.get("server") != "none" or ready_fields.get("trace_schema") != TRACE_SCHEMA:
        raise ParseError("boot marker mode or identity mismatch")
    if mode == "scripted":
        if len(harness) != 1:
            raise ParseError("missing or duplicate LAB04_HARNESS_DONE marker")
        harness_fields = _marker_fields(harness[0], "LAB04_HARNESS_DONE ")
        if (harness_fields.get("status") != "complete" or harness_fields.get("variants") != "3"
                or harness_fields.get("checkpoints") not in {"0", "1"}):
            raise ParseError("invalid LAB04_HARNESS_DONE marker")
    if mode == "manual" and len(harness) > 1:
        raise ParseError("duplicate LAB04_HARNESS_DONE marker")
    return {"ready": ready, "harness": harness}, mode != "scripted" or bool(harness)


def classify_experiment(scenarios: dict[str, dict[str, Any]], inputs: list[str]) -> str:
    """Score only decided inputs; exploratory observations remain descriptive."""
    if not inputs:
        return "UNMEASURED"
    if any(key not in scenarios for key in inputs):
        return "INCOMPLETE"
    if any(scenarios[key].get("invalid_evidence", False) or
           (scenarios[key].get("inventory_complete", True) and
            scenarios[key].get("terminal_count") == 1 and
            scenarios[key].get("terminal") != DECIDED[key]) for key in inputs):
        return "FAIL"
    if any(not scenarios[key].get("inventory_complete", True) for key in inputs):
        return "INCOMPLETE"
    if any(scenarios[key].get("terminal_count") != 1
           or scenarios[key].get("terminal") != DECIDED.get(key)
           for key in inputs):
        return "FAIL"
    return "PASS"


def _capture_provenance(capture_context: Mapping[str, Any] | None) -> tuple[str, dict[str, Any]]:
    """Accept hardware provenance only from explicit adapter capture evidence."""
    if capture_context is None:
        return "host", {"kind": "offline_or_fixture_parse", "status": "unmeasured"}
    if (capture_context.get("kind") != "device_serial_capture" or
            not isinstance(capture_context.get("target_id"), str) or not capture_context["target_id"].strip() or
            capture_context.get("post_reset") is not True):
        raise ParseError("invalid device capture provenance context")
    return "hardware", {"kind": "device_serial_capture", "target_id": capture_context["target_id"],
                         "post_reset": True}


def _measurement_series(rows: list[dict[str, Any]], provenance: str) -> dict[str, Any]:
    """Aggregate real samples using nearest-rank percentiles; retain raw samples in trace_rows."""
    result: dict[str, Any] = {}
    for name, (field, unit) in _SERIES_FIELDS.items():
        samples = []
        raster_accounting_missing = 0
        for row in rows:
            if field not in row:
                continue
            scenario = row.get("scenario")
            if field in {"encode_us", "decode_us", "internal_largest_free_block_bytes",
                         "psram_largest_free_block_bytes", "lvgl_largest_free_block_bytes"}:
                if scenario not in _ENCODING_SCENARIOS or row.get("reason") == "preview_alloc_failed":
                    continue
            if field == "decode_us" and row.get("encoded_bytes", 0) == 0:
                continue
            if field == "seek_us" and scenario != "F-checkpoint":
                continue
            # Firmware reports zero raster counters when no update ran. Such a
            # row has no raster timing observation, including failed operations.
            if field == "raster_us":
                if not {"raster_work_cap", "raster_eligible", "raster_rendered", "raster_remaining"}.issubset(row):
                    raster_accounting_missing += 1
                    continue
                if row.get("terminal") != "success" or row["raster_eligible"] == 0:
                    continue
            samples.append(row[field])
        ordered = sorted(samples)
        count = len(ordered)
        summary: dict[str, Any] = {
            "unit": unit,
            "provenance": provenance if count else "unmeasured/blocked",
            "sample_count": count,
            "samples": samples,
        }
        for label, percentile in _PERCENTILES:
            rank = max(1, math.ceil(percentile * count / 100)) if count else None
            summary[label] = ordered[rank - 1] if rank else None
        summary["worst"] = ordered[-1] if count else None
        summary["missing_reason"] = (
            None if count else
            "raster work accounting absent in legacy v1 rows; eligibility cannot be established"
            if name == "raster_us" and raster_accounting_missing else
            "no eligible samples in trace"
        )
        result[name] = summary
    return result


def parse(serial: str, run_id: str, *, mode: str = "manual", variant: str | None = None,
          post_reset: bool = True,
          capture_context: Mapping[str, Any] | None = None,
          expected_build_id: str | None = None) -> dict[str, Any]:
    if not isinstance(run_id, str) or not _RUN_ID.fullmatch(run_id) or run_id in {".", ".."}:
        raise ParseError("invalid run_id")
    if mode not in {"manual", "scripted"}:
        raise ParseError("invalid mode")
    if variant is not None and variant != mode:
        raise ParseError("variant/phase mismatch")
    if not post_reset:
        raise ParseError("success markers were observed before reset")
    markers, _ = _markers(serial, mode, expected_build_id)
    provenance, provenance_context = _capture_provenance(capture_context)
    rows, firmware_loss, publication_loss, resource_samples, resource_peaks, trace_reconciliation = _trace_rows(serial)
    seen = {r["scenario"] for r in rows}
    if mode == "scripted" and not (set(DECIDED) | EXPLORATORY).issubset(seen):
        raise ParseError("missing decided or exploratory scenario samples")
    for required_variant in ("points", "strokes", "preview"):
        if mode == "scripted" and not any(r["scenario"] in EXPLORATORY and r["variant"] == required_variant and "encoded_bytes" in r for r in rows):
            raise ParseError(f"missing encoded byte observation for {required_variant}")
    scenarios: dict[str, dict[str, Any]] = {}
    for scenario in set(DECIDED) | EXPLORATORY:
        members = [r for r in rows if r["scenario"] == scenario]
        if not members:
            continue
        if scenario in DECIDED:
            expected = DECIDED[scenario]
            expected_counts = DECIDED_INVENTORY[scenario]
            actual_counts = {variant: sum(r["variant"] == variant for r in members)
                             for variant in expected_counts}
            wrong_variant = any(r["variant"] not in expected_counts for r in members)
            expected_reasons = DECIDED_REASONS[scenario]
            reason_counts_match = all(
                {reason: sum(r["variant"] == variant and r.get("reason", "") == reason for r in members)
                 for reason in expected_reasons[variant]} ==
                {reason: 1 for reason in expected_reasons[variant]}
                for variant in expected_counts)
            inventory_complete = (actual_counts == expected_counts and reason_counts_match and
                                  all(r["fixture"] == "corpus-b" for r in members))
            wrong_reason = any(r["variant"] in expected_reasons and
                               r.get("reason", "") not in expected_reasons[r["variant"]]
                               for r in members)
            valid_terminal = all(r["terminal"] == expected and r["terminal_count"] == 1
                                 for r in members)
            observed_failure = wrong_variant or wrong_reason or (inventory_complete and not valid_terminal)
            terminal = "failure" if observed_failure else (expected if inventory_complete else "incomplete")
            scenarios[scenario] = {"class": "decided", "terminal": terminal,
                                   "terminal_count": 1 if inventory_complete and valid_terminal else 0,
                                   "sample_count": len(members), "inventory_complete": inventory_complete,
                                   "invalid_evidence": wrong_variant or wrong_reason or (inventory_complete and not valid_terminal),
                                   "expected_variant_counts": expected_counts,
                                   "actual_variant_counts": actual_counts, "samples": members}
        else:
            # Keep a compact required observation object and preserve every
            # per-fixture/per-variant measurement alongside it in `samples`.
            chosen = next((r for r in members if r.get("fixture") == "corpus-b"), members[0])
            observation = {"terminal": chosen["terminal"]}
            if scenario in {"F-points", "F-strokes", "F-preview"}:
                observation["encoded_bytes"] = chosen["encoded_bytes"]
            scenarios[scenario] = {"class": "exploratory", "observation": observation,
                                   "sample_count": len(members), "samples": members}
    inputs = ["F-malformed", "F-overflow", "F-cancel"]
    verdict = classify_experiment(scenarios, inputs)
    summary = trace_reconciliation["summary"]
    reconciliation = trace_reconciliation["reconciliation"]
    publication_reconciled = (publication_loss is not None and summary["status"] == "OBSERVED"
                               and publication_loss["dropped_events"] == summary["publication_queue_drops"])
    if (summary["status"] != "OBSERVED" or reconciliation["status"] != "COMPLETE"
            or summary["serial_write_errors"] != 0 or summary["publication_queue_drops"] != 0
            or not publication_reconciled):
        if verdict == "PASS":
            verdict = "INCOMPLETE"
    # This firmware trace schema does not report device-side dropped-event
    # counters. Do not infer zero loss from parser success or host capture.
    measurements = _measurement_series(rows, provenance)
    raster_counter_fields = {"raster_work_cap", "raster_eligible", "raster_rendered", "raster_remaining"}
    rows_with_raster_accounting = sum(raster_counter_fields.issubset(row) for row in rows)
    if rows_with_raster_accounting == len(rows):
        raster_work_accounting = {"status": "OBSERVED", "rows_with_counters": rows_with_raster_accounting,
                                  "rows_missing_counters": 0}
    elif rows_with_raster_accounting == 0:
        raster_work_accounting = {"status": "MISSING", "rows_with_counters": 0,
                                  "rows_missing_counters": len(rows),
                                  "missing_reason": "legacy v1 rows predate raster work counters"}
    else:
        raster_work_accounting = {"status": "PARTIAL", "rows_with_counters": rows_with_raster_accounting,
                                  "rows_missing_counters": len(rows) - rows_with_raster_accounting,
                                  "missing_reason": "legacy v1 rows predate raster work counters"}
    missing = [name for name, series in measurements.items() if series["sample_count"] == 0]
    if firmware_loss is None:
        missing.insert(0, "firmware_trace_loss")
    if publication_loss is None:
        missing.append("firmware_publication_queue")
    if summary["status"] != "OBSERVED":
        missing.append("trace_summary")
    if reconciliation["status"] != "COMPLETE":
        missing.append("trace_sequence_reconciliation")
    if summary["serial_write_errors"] not in (None, 0):
        missing.append("firmware_serial_write_errors")
    if summary["publication_queue_drops"] not in (None, 0):
        missing.append("firmware_publication_queue_drops")
    if (publication_loss is not None and summary["publication_queue_drops"] is not None
            and publication_loss["dropped_events"] != summary["publication_queue_drops"]):
        missing.append("publication_loss_summary_mismatch")
    if not resource_samples:
        missing.append("device_resource_samples")
    if rows_with_raster_accounting != len(rows):
        missing.append("raster_work_accounting")
    if mode == "scripted" and any(not scenarios.get(key, {}).get("inventory_complete", False)
                                   for key in DECIDED):
        missing.append("complete_decided_sample_inventory")
    queue_drops = summary["publication_queue_drops"]
    write_errors = summary["serial_write_errors"]
    evidence = {
        "status": "COMPLETE" if summary["status"] == "OBSERVED" and reconciliation["status"] == "COMPLETE"
        and queue_drops == 0 and write_errors == 0 and publication_reconciled else "INCOMPLETE",
        "trace_loss": ("OBSERVED" if (queue_drops or write_errors or
                           firmware_loss is not None and any(firmware_loss[key] > 0 for key in
                               ("dropped_events", "oversized_events", "malformed_rows")) or
                           publication_loss is not None and publication_loss["dropped_events"] > 0) else
                       "UNKNOWN" if summary["status"] != "OBSERVED" or not publication_reconciled
                       or firmware_loss is None else "NONE"),
        "firmware_trace_loss": firmware_loss or {"status": "UNKNOWN", "dropped_events": None,
                                                  "oversized_events": None, "malformed_rows": None},
        "firmware_publication_queue": publication_loss or {
            "status": "UNKNOWN", "source": "firmware_publication_queue", "dropped_events": None},
        "trace_summary": summary,
        "trace_reconciliation": reconciliation,
        "missing": missing,
    }
    return {
        "document": PARSER_DOCUMENT,
        "run_id": run_id,
        "schema_version": TRACE_SCHEMA,
        "parser_version": PARSER_VERSION,
        "evidence": evidence,
        "lab_id": "LAB-04",
        "trace_schema": TRACE_SCHEMA,
        "mode": mode,
        "variant": variant,
        "markers": markers,
        "lab_metrics": {"experiment_inputs": inputs, "experiment_verdict": verdict,
                        "performance_verdict": "UNMEASURED",
                        "measurements": measurements,
                        "device_resource_samples": {
                            "status": "OBSERVED" if resource_samples else "MISSING",
                            "measurement_kind": "point_observation_not_peak",
                            "sample_count": len(resource_samples), "samples": resource_samples,
                            "missing_reason": None if resource_samples else
                            "legacy capture or no eligible preview resource sample"},
                        "device_resource_peak": {
                            "status": "OBSERVED" if resource_peaks else "MISSING",
                            "measurement_kind": "decoder_maximum_output_capacity_reservation",
                            "whole_system_temporal_peak": False,
                            "sample_count": len(resource_peaks), "samples": resource_peaks,
                            "missing_reason": None if resource_peaks else "legacy capture or no decoder reservation observation"},
                        "raster_work_accounting": raster_work_accounting,
                        "scenarios": scenarios},
        "trace_rows": len(rows),
        "evidence_provenance": provenance,
        "provenance_context": provenance_context,
    }


def parse_file(serial_path: Path, output_path: Path | None = None, *, expected_run_id: str | None = None,
               expected_mode: str = "manual", expected_variant: str | None = None,
               post_reset: bool = True,
               capture_context: Mapping[str, Any] | None = None,
               expected_build_id: str | None = None) -> dict[str, Any]:
    data = serial_path.read_bytes()
    if len(data) > MAX_INPUT_BYTES:
        raise ParseError("serial input exceeds byte limit")
    try:
        decoded = data.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise ParseError("serial is not valid UTF-8") from exc
    output = parse(decoded, expected_run_id or serial_path.parent.name,
                   mode=expected_mode, variant=expected_variant, post_reset=post_reset,
                   capture_context=capture_context, expected_build_id=expected_build_id)
    if output_path is not None:
        encoded = json.dumps(output, ensure_ascii=False, allow_nan=False, separators=(",", ":"))
        if len(encoded.encode("utf-8")) > MAX_OUTPUT_BYTES:
            raise ParseError("metrics output exceeds byte limit")
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(json.dumps(output, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("serial", type=Path)
    parser.add_argument("-o", "--out", required=True, type=Path)
    parser.add_argument("--mode", choices=("manual", "scripted"), default="manual")
    parser.add_argument("--variant")
    args = parser.parse_args()
    parse_file(args.serial, args.out, expected_mode=args.mode, expected_variant=args.variant)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
