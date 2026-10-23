#!/usr/bin/env python3
"""Strict parser for LAB-15 capture memory envelope serial evidence."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

PARSER_DOCUMENT = "family-link.lab-metrics/v2"
TRACE_SCHEMA = "lab15-envelope-trace/v1"
PARSER_VERSION = "lab15-parser/v1"
MAX_INPUT_BYTES = 2 * 1024 * 1024
MAX_OUTPUT_BYTES = 4 * 1024 * 1024
MAX_LINES = 16384
MAX_LINE_BYTES = 4096
EXPERIMENT_INPUTS = [
    "E-baseline-ui",
    "E-catalog-ui",
    "E-catalog-peak-only",
    "E-chunk-fragmentation",
]
EXPLORATORY = ("X-opus-link", "X-sdmmc-spill", "X-realtime-180s")
_KV = re.compile(r"([a-z][a-z0-9_]*)=([^\s]*)")
_EVIDENCE_BLOCKED = re.compile(
    r"(?i)(?:\bpin\s*[=:]\s*\S+|\b(?:media_payload|payload_b64|image_data|audio_data)\s*[=:])"
)
_RESOURCE_FIELDS = {
    "schema",
    "event",
    "seq",
    "t_us",
    "arm",
    "phase",
    "live_nodes",
    "internal_free",
    "internal_largest_free", "internal_min_free", "psram_free", "psram_largest_free",
    "lvgl_used", "lvgl_largest_free", "dma_largest_free",
}
_ALLOC_FIELDS = {
    "schema", "event", "seq", "t_us", "arm", "kind", "bytes", "result", "region",
}
_TERMINAL_FIELDS = {
    "schema", "event", "seq", "t_us", "arm", "operation_id", "outcome", "reason",
}
_UINT_FIELDS = {
    "seq", "t_us", "live_nodes", "internal_free", "internal_largest_free", "internal_min_free",
    "psram_free", "psram_largest_free", "lvgl_used", "lvgl_largest_free", "dma_largest_free",
    "bytes", "operation_id",
}


class ParseError(ValueError):
    pass


def validate_evidence_line(line: str) -> None:
    if _EVIDENCE_BLOCKED.search(line):
        raise ParseError("serial line contains a raw PIN or media payload")


def redact_line(line: str) -> str:
    return line


def _marker_fields(line: str, prefix: str) -> dict[str, str]:
    body = line[len(prefix):]
    matches = list(_KV.finditer(body))
    if not matches or " ".join(m.group(0) for m in matches) != body.strip():
        raise ParseError(f"malformed {prefix.strip()} marker")
    out: dict[str, str] = {}
    for match in matches:
        key, value = match.groups()
        if key in out:
            raise ParseError(f"duplicate marker field {key}")
        out[key] = value
    return out


def _trace_fields(line: str) -> dict[str, str]:
    body = line[len("LAB15_TRACE "):].strip()
    matches = list(_KV.finditer(body))
    if not matches or " ".join(m.group(0) for m in matches) != body:
        raise ParseError("malformed LAB15_TRACE row")
    out: dict[str, str] = {}
    for match in matches:
        key, value = match.groups()
        if key in out:
            raise ParseError(f"duplicate trace field {key}")
        out[key] = value
    return out


def _uint(value: str, key: str) -> int:
    if not re.fullmatch(r"0|[1-9][0-9]*", value):
        raise ParseError(f"invalid integer {key}")
    parsed = int(value)
    if key in _UINT_FIELDS and parsed < 0:
        raise ParseError(f"negative integer {key}")
    return parsed


def _coerce_row(fields: dict[str, str]) -> dict[str, Any]:
    row: dict[str, Any] = dict(fields)
    for key in list(row):
        if key in _UINT_FIELDS:
            row[key] = _uint(str(row[key]), key)
    return row


def evaluate_experiment(
    *,
    arm: str | None,
    terminals: dict[int, dict[str, Any]],
    allocations: list[dict[str, Any]],
    resource_samples: list[dict[str, Any]],
) -> str:
    if not terminals:
        return "UNMEASURED"
    if len(terminals) != 1:
        return "FAIL"
    terminal = next(iter(terminals.values()))
    outcome = terminal.get("outcome")
    if outcome == "allocation_failure":
        return "FAIL"
    if outcome not in {"duration_cap", "peak_alloc_complete"}:
        return "FAIL"
    pcm_kind = "pcm_chunk" if "_chunk_" in (arm or "") or arm and "chunk" in arm else "pcm_ring"
    if arm and "chunk" in arm.split("_")[1:2]:
        pcm_kind = "pcm_chunk"
    if "chunk" in (arm or ""):
        pcm_kind = "pcm_chunk"
    pcm_attempts = [row for row in allocations if row.get("kind") in {"pcm_ring", "pcm_chunk"}]
    if not pcm_attempts or any(row.get("result") != "ok" for row in pcm_attempts):
        return "FAIL"
    ui_samples = [row for row in resource_samples if row.get("phase") == "ui_mounted"]
    if arm and arm.startswith("catalog_") and ui_samples:
        if ui_samples[0].get("live_nodes", 0) == 0:
            return "FAIL"
    if arm and arm.startswith("none_") and ui_samples:
        if ui_samples[0].get("live_nodes", 0) != 0:
            return "FAIL"
    return "PASS"


def parse(
    serial: str,
    *,
    run_id: str = "lab15-run",
    mode: str = "scripted",
    capture_context: dict[str, Any] | None = None,
) -> dict[str, Any]:
    validate_evidence_line(serial)
    encoded = serial.encode("utf-8")
    if len(encoded) > MAX_INPUT_BYTES:
        raise ParseError("serial exceeds byte bound")
    lines = serial.splitlines()
    if len(lines) > MAX_LINES:
        raise ParseError("serial exceeds line bound")

    ready: dict[str, str] | None = None
    harness_done = False
    resource_samples: list[dict[str, Any]] = []
    allocations: list[dict[str, Any]] = []
    terminals: dict[int, dict[str, Any]] = {}
    arm: str | None = None

    for line in lines:
        if len(line.encode("utf-8")) > MAX_LINE_BYTES:
            raise ParseError("line exceeds byte bound")
        if line.startswith("LAB15_READY "):
            ready = _marker_fields(line, "LAB15_READY ")
            if ready.get("harness") not in {"0", "1"}:
                raise ParseError("LAB15_READY harness must be 0 or 1")
            arm = ready.get("arm")
            continue
        if line.startswith("LAB15_HARNESS_DONE "):
            fields = _marker_fields(line, "LAB15_HARNESS_DONE ")
            if fields.get("status") != "complete":
                raise ParseError("LAB15_HARNESS_DONE status must be complete")
            harness_done = True
            continue
        if line.startswith("LAB15_INIT_FAILED "):
            raise ParseError("firmware reported LAB15_INIT_FAILED")
        if not line.startswith("LAB15_TRACE "):
            continue
        fields = _trace_fields(line)
        if fields.get("schema") != TRACE_SCHEMA:
            raise ParseError("wrong trace schema")
        event = fields.get("event")
        row = _coerce_row(fields)
        if arm is None and "arm" in row:
            arm = str(row["arm"])
        if event == "resource_sample":
            if set(fields) != _RESOURCE_FIELDS:
                raise ParseError("malformed resource_sample fields")
            resource_samples.append(row)
        elif event == "allocation_attempt":
            if set(fields) != _ALLOC_FIELDS:
                raise ParseError("malformed allocation_attempt fields")
            if fields.get("result") not in {"ok", "fail"}:
                raise ParseError("allocation_attempt result invalid")
            allocations.append(row)
        elif event == "terminal":
            if set(fields) != _TERMINAL_FIELDS:
                raise ParseError("malformed terminal fields")
            op_id = int(row["operation_id"])
            if op_id in terminals:
                raise ParseError("duplicate terminal for operation_id")
            terminals[op_id] = row
        else:
            raise ParseError(f"unknown trace event {event}")

    if ready is None:
        raise ParseError("missing LAB15_READY")
    if mode == "scripted" and not harness_done:
        raise ParseError("missing LAB15_HARNESS_DONE")

    provenance = "hardware"
    if capture_context and capture_context.get("kind") != "device_serial_capture":
        provenance = "host"

    experiment_verdict = evaluate_experiment(
        arm=arm,
        terminals=terminals,
        allocations=allocations,
        resource_samples=resource_samples,
    )
    workflow_status = "PASS" if (mode != "scripted" or harness_done) and ready else "FAIL"
    if mode == "scripted" and not harness_done:
        workflow_status = "FAIL"

    return {
        "document": PARSER_DOCUMENT,
        "parser_version": PARSER_VERSION,
        "trace_schema": TRACE_SCHEMA,
        "run_id": run_id,
        "lab_id": "LAB-15",
        "mode": mode,
        "workflow_status": workflow_status,
        "experiment_verdict": experiment_verdict,
        "experiment_inputs": list(EXPERIMENT_INPUTS),
        "exploratory_inputs": list(EXPLORATORY),
        "ready": ready,
        "harness_done": harness_done,
        "arm": arm,
        "resource_samples": resource_samples,
        "allocation_attempts": allocations,
        "terminals": list(terminals.values()),
        "provenance": provenance,
    }


def parse_file(path: Path, *, output_path: Path | None = None, **kwargs: Any) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8")
    result = parse(text, **kwargs)
    payload = json.dumps(result, indent=2, sort_keys=True)
    if len(payload.encode("utf-8")) > MAX_OUTPUT_BYTES:
        raise ParseError("metrics output exceeds bound")
    if output_path is not None:
        output_path.write_text(payload + "\n", encoding="utf-8")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Parse LAB-15 serial trace")
    parser.add_argument("serial", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    parser.add_argument("--run-id", default="lab15-run")
    parser.add_argument("--mode", default="scripted", choices=("manual", "scripted"))
    args = parser.parse_args()
    try:
        parse_file(args.serial, output_path=args.output, run_id=args.run_id, mode=args.mode)
    except ParseError as exc:
        print(exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    import sys

    raise SystemExit(main())
