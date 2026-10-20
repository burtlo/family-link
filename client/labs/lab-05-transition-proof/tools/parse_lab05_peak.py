#!/usr/bin/env python3
"""Parse LAB-05 peak serial text into family-link.lab-metrics/v2.

Nearest-rank percentiles are computed here. This module does not import
another lab's aggregate helper. Missing measurements stay null.
"""
from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path
from typing import Any, Mapping

DOCUMENT = "family-link.lab-metrics/v2"
SCHEMA_VERSION = "lab05-peak-trace/v1"
PARSER_VERSION = "lab05-peak-parser/v2"
EXPERIMENT_INPUTS = ["P-alloc-fail", "P-stale-callback", "P-cancel"]
EXPLORATORY = ("P-overlap", "P-replace")
DECIDED_TERMINALS = {
    "P-alloc-fail": ("failure", "alloc"),
    "P-stale-callback": ("stale", None),
    "P-cancel": ("cancelled", None),
}
SERIES_FIELDS = {
    "lvgl_high_water_bytes": "bytes",
    "lvgl_largest_free_bytes": "bytes",
    "internal_largest_free_bytes": "bytes",
    "psram_largest_free_bytes": "bytes",
    "anim_frame_us": "us",
    "temp_image_bytes": "bytes",
}
_ABSENT = {"unmeasured", "unavailable"}
_KV = re.compile(r"([A-Za-z][A-Za-z0-9_]*)=([^\s]+)")
_RUN_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$")
_BLOCKED = re.compile(
    r"(?i)(?:\bpin\s*[=:]\s*\S+|\b(?:media_payload|payload_b64|image_data|audio_data)\s*[=:])"
)
_UINT = re.compile(r"^[0-9]+$")
MAX_INPUT_BYTES = 4 * 1024 * 1024
MAX_LINE_BYTES = 4096
_U64 = 2**64 - 1


class ParseError(ValueError):
    """Serial text cannot be trusted as LAB-05 peak evidence."""


def validate_evidence_line(line: str) -> None:
    """Reject a raw PIN or media payload before it can be stored."""
    if _BLOCKED.search(line):
        raise ParseError("serial line contains a raw PIN or media payload")


def nearest_rank_summary(samples: list[Any]) -> dict[str, Any]:
    """Sort finite samples and emit nearest-rank p50, p95, p99, and max.

    Rank is one-based ceil(p * n / 100), clamped to [1, n]. An empty series
    is null percentiles, sample_count 0, and a missing_reason. Values are
    never invented as zero.
    """
    finite = sorted(
        value
        for value in samples
        if isinstance(value, (int, float))
        and not isinstance(value, bool)
        and math.isfinite(value)
    )
    count = len(finite)
    if count == 0:
        return {
            "p50": None,
            "p95": None,
            "p99": None,
            "max": None,
            "sample_count": 0,
            "missing_reason": "no finite samples",
            "provenance": "unmeasured/blocked",
        }
    summary: dict[str, Any] = {
        "sample_count": count,
        "missing_reason": None,
        "provenance": "host",
        "max": finite[-1],
    }
    for label, percentile in (("p50", 50), ("p95", 95), ("p99", 99)):
        rank = (percentile * count + 99) // 100
        if rank < 1:
            rank = 1
        if rank > count:
            rank = count
        summary[label] = finite[rank - 1]
    return summary


def classify_experiment(scenarios: Mapping[str, Any], inputs: list[str]) -> str:
    """Score decided inputs only. An empty input list is UNMEASURED.

    Exploratory observations are ignored. A decided row passes only with
    terminal_count 1 and the terminal (and reason, when required) from the
    scenario table. Workflow status is not an input.
    """
    if not inputs:
        return "UNMEASURED"
    for scenario_id in inputs:
        row = scenarios.get(scenario_id)
        if not isinstance(row, dict):
            return "FAIL"
        observation = row.get("observation") if isinstance(row.get("observation"), dict) else {}
        terminal = row.get("terminal", observation.get("terminal"))
        count = row.get("terminal_count", observation.get("terminal_count"))
        reason = row.get("reason", observation.get("reason"))
        expected = DECIDED_TERMINALS.get(scenario_id)
        if expected is None or count != 1 or terminal != expected[0]:
            return "FAIL"
        if expected[1] is not None and reason != expected[1]:
            return "FAIL"
    return "PASS"


def _fields(body: str, label: str) -> dict[str, str]:
    matches = list(_KV.finditer(body.strip()))
    if not matches or " ".join(match.group(0) for match in matches) != body.strip():
        raise ParseError(f"malformed {label}")
    found: dict[str, str] = {}
    for match in matches:
        key, value = match.group(1), match.group(2)
        if key in found:
            raise ParseError(f"duplicate trace field: {key}")
        found[key] = value
    return found


def _u64(token: str, field: str) -> int:
    if not _UINT.fullmatch(token) or len(token) > 20:
        raise ParseError(f"malformed or overflow {field}")
    value = int(token)
    if value > _U64:
        raise ParseError(f"overflow {field}")
    return value


def _optional_samples(token: str, field: str) -> list[int]:
    if token in _ABSENT:
        return []
    parts = token.split(",")
    if not parts or any(not _UINT.fullmatch(part) or len(part) > 20 for part in parts):
        raise ParseError(f"malformed {field}")
    values = [int(part) for part in parts]
    if any(value > _U64 for value in values):
        raise ParseError(f"overflow {field}")
    return values


def _variant_control(variant: str | None) -> str | None:
    if variant in {"replace", "scripted-replace"}:
        return "replace"
    if variant in {"manual", "scripted", None}:
        return "persistent" if variant else None
    raise ParseError(f"unknown variant: {variant}")


def _operation_result(row: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "operation_id": row["operation_id"],
        "kind": row["kind"],
        "terminal": row["terminal"],
        "terminal_count": row["terminal_count"],
        "reason": row["reason"],
        "control": row["control"],
    }


def _normalize_kinds(scenario_id: str, rows: list[dict[str, Any]]) -> None:
    """Score one repetition row. Worker rows stay separate operation results.

    A row without kind is a repetition when it is the only row. A kind-less
    decided log with several rows keeps the single row that matches the
    scenario terminal and treats the others as workers.
    """
    if not rows:
        return
    if all(row["kind"] is not None for row in rows):
        return
    if any(row["kind"] is not None for row in rows):
        for row in rows:
            if row["kind"] is None:
                row["kind"] = "repetition"
        return
    if len(rows) == 1 or scenario_id not in DECIDED_TERMINALS:
        for row in rows:
            row["kind"] = "repetition"
        return
    expected = DECIDED_TERMINALS[scenario_id]
    matches = [
        row
        for row in rows
        if row["terminal"] == expected[0] and (expected[1] is None or row["reason"] == expected[1])
    ]
    if len(matches) != 1:
        raise ParseError(f"duplicate decided scenario rows: {scenario_id}")
    chosen = matches[0]
    for row in rows:
        row["kind"] = "repetition" if row is chosen else "worker"


def _scenario_shell(rows: list[dict[str, Any]]) -> dict[str, Any]:
    operations = [_operation_result(row) for row in rows]
    scored = [row for row in rows if row.get("kind") == "repetition"]
    if not rows:
        return {
            "terminal": None,
            "terminal_count": None,
            "reason": None,
            "observation": {
                "terminal": None,
                "terminal_count": None,
                "repetition_count": None,
                "missing_reason": "no trace rows",
            },
            "operations": [],
        }
    if not scored:
        return {
            "terminal": None,
            "terminal_count": None,
            "reason": None,
            "observation": {
                "terminal": None,
                "terminal_count": None,
                "repetition_count": 0,
                "reason": None,
                "live_image_count": rows[-1]["live_image_count"],
                "control": None,
                "missing_reason": "no repetition row",
            },
            "operations": operations,
        }
    terminals = [row["terminal"] for row in scored]
    terminal = terminals[0] if len(set(terminals)) == 1 else "mixed"
    counts = [row["terminal_count"] for row in scored]
    count = counts[0] if len(scored) == 1 else None
    reasons = [row["reason"] for row in scored]
    reason = reasons[0] if len(set(reasons)) == 1 else None
    controls = {row["control"] for row in scored}
    control = next(iter(controls)) if len(controls) == 1 else None
    return {
        "terminal": terminal if len(scored) == 1 else None,
        "terminal_count": count,
        "reason": reason if len(scored) == 1 else None,
        "observation": {
            "terminal": terminal,
            "terminal_count": counts[0] if len(set(counts)) == 1 else None,
            "repetition_count": len(scored),
            "reason": reason,
            "live_image_count": scored[-1]["live_image_count"],
            "control": control,
        },
        "operations": operations,
    }


def _capture_provenance(capture_context: Mapping[str, Any] | None) -> tuple[str, dict[str, Any]]:
    """Hardware provenance requires an explicit post-reset device capture.

    A missing context is an offline or fixture parse and stays host.
    Any other context is rejected so a host label cannot be upgraded by accident.
    """
    if capture_context is None:
        return "host", {"kind": "offline_or_fixture_parse", "status": "unmeasured"}
    target_id = capture_context.get("target_id")
    if (
        capture_context.get("kind") != "device_serial_capture"
        or not isinstance(target_id, str)
        or not target_id.strip()
        or capture_context.get("post_reset") is not True
    ):
        raise ParseError("invalid device capture provenance context")
    return "hardware", {
        "kind": "device_serial_capture",
        "target_id": target_id,
        "post_reset": True,
    }


def parse(
    serial: str,
    run_id: str,
    *,
    mode: str = "manual",
    variant: str | None = None,
    post_reset: bool = True,
    capture_context: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Reduce one serial capture. run_id is copied into the metrics document."""
    provenance, provenance_context = _capture_provenance(capture_context)
    if not isinstance(run_id, str) or not _RUN_ID.fullmatch(run_id) or run_id in {".", ".."}:
        raise ParseError("invalid run_id")
    if mode not in {"manual", "scripted"}:
        raise ParseError("invalid mode")
    if not post_reset:
        raise ParseError("success markers were observed before reset")
    expected_control = _variant_control(variant)
    if len(serial.encode("utf-8")) > MAX_INPUT_BYTES:
        raise ParseError("serial input exceeds byte limit")

    rows: list[dict[str, Any]] = []
    seen_ids: set[int] = set()
    ready = 0
    harness_complete = 0
    series_samples: dict[str, list[int]] = {name: [] for name in SERIES_FIELDS}
    for raw_line in serial.splitlines():
        if len(raw_line.encode("utf-8")) > MAX_LINE_BYTES:
            raise ParseError("serial line exceeded byte bound")
        line = raw_line.strip()
        if not line:
            continue
        validate_evidence_line(line)
        if line.startswith("LAB05_PEAK_READY "):
            fields = _fields(line[len("LAB05_PEAK_READY "):], "LAB05_PEAK_READY")
            if "status" in fields:
                continue
            ready += 1
            continue
        if line.startswith("LAB05_PEAK_HARNESS_DONE "):
            fields = _fields(line[len("LAB05_PEAK_HARNESS_DONE "):], "LAB05_PEAK_HARNESS_DONE")
            if fields.get("status") == "complete":
                harness_complete += 1
            continue
        if not line.startswith("LAB05_PEAK_TRACE "):
            continue
        fields = _fields(line[len("LAB05_PEAK_TRACE "):], "LAB05_PEAK_TRACE")
        missing = [
            key
            for key in (
                "scenario",
                "control",
                "terminal",
                "operation_id",
                "terminal_count",
                "live_image_count",
                "class",
                "reason",
                "temp_image_bytes",
            )
            if key not in fields
        ]
        if missing:
            raise ParseError("missing trace field: " + ",".join(missing))
        scenario = fields["scenario"]
        if scenario not in DECIDED_TERMINALS and scenario not in EXPLORATORY:
            raise ParseError(f"unknown scenario: {scenario}")
        expected_class = "exploratory" if scenario in EXPLORATORY else "decided"
        if fields["class"] != expected_class:
            raise ParseError(f"scenario class mismatch: {scenario}")
        if fields["control"] not in {"persistent", "replace"}:
            raise ParseError("unknown control")
        if fields["terminal"] not in {"success", "failure", "cancelled", "stale", "timed_out"}:
            raise ParseError("unknown terminal")
        operation_id = _u64(fields["operation_id"], "operation_id")
        if operation_id in seen_ids:
            raise ParseError(f"duplicate operation_id: {operation_id}")
        seen_ids.add(operation_id)
        terminal_count = _u64(fields["terminal_count"], "terminal_count")
        if terminal_count != 1:
            raise ParseError("duplicate or missing terminal outcome")
        kind = fields.get("kind")
        if kind is not None and kind not in {"repetition", "worker", "other"}:
            raise ParseError("unknown operation kind")
        row = {
            "scenario": scenario,
            "control": fields["control"],
            "terminal": fields["terminal"],
            "operation_id": operation_id,
            "terminal_count": terminal_count,
            "live_image_count": _u64(fields["live_image_count"], "live_image_count"),
            "class": expected_class,
            "reason": fields["reason"],
            "temp_image_bytes": _u64(fields["temp_image_bytes"], "temp_image_bytes"),
            "kind": kind,
        }
        rows.append(row)
        for name in SERIES_FIELDS:
            if name not in fields:
                continue
            if name == "anim_frame_us":
                samples = _optional_samples(fields[name], name)
                declared = fields.get("anim_frame_us_count")
                if declared is not None and _u64(declared, "anim_frame_us_count") != len(samples) and fields[name] not in _ABSENT:
                    raise ParseError("anim_frame_us_count does not match samples")
                if fields[name] not in _ABSENT:
                    series_samples[name].extend(samples)
                continue
            if fields[name] in _ABSENT:
                continue
            series_samples[name].append(_u64(fields[name], name))

    if ready > 1 or harness_complete > 1:
        raise ParseError("duplicate lifecycle marker")
    grouped: dict[str, list[dict[str, Any]]] = {name: [] for name in (*EXPLORATORY, *DECIDED_TERMINALS)}
    for row in rows:
        grouped[row["scenario"]].append(row)
    for scenario_id, scenario_rows in grouped.items():
        _normalize_kinds(scenario_id, scenario_rows)
    for scenario_id in DECIDED_TERMINALS:
        repetition_rows = [row for row in grouped[scenario_id] if row["kind"] == "repetition"]
        if len(repetition_rows) > 1:
            raise ParseError(f"duplicate decided scenario rows: {scenario_id}")
    decided_measured = any(
        any(row["kind"] == "repetition" for row in grouped[scenario_id])
        for scenario_id in DECIDED_TERMINALS
    )
    controls = {row["control"] for row in rows}
    control = next(iter(controls)) if len(controls) == 1 else None
    if expected_control is not None and rows and control != expected_control:
        control = None
    scenarios: dict[str, Any] = {}
    for scenario_id in EXPLORATORY:
        shell = _scenario_shell(grouped[scenario_id])
        scenarios[scenario_id] = {"class": "exploratory", **shell}
    for scenario_id in DECIDED_TERMINALS:
        shell = _scenario_shell(grouped[scenario_id])
        scenarios[scenario_id] = {"class": "decided", **shell}
    verdict = classify_experiment(scenarios, list(EXPERIMENT_INPUTS))
    marker_ok = ready == 1 and (mode != "scripted" or harness_complete == 1)
    if not marker_ok or not rows:
        verdict = "UNMEASURED"
    elif control is None and verdict == "PASS":
        verdict = "UNMEASURED"
    elif control == "replace" and not decided_measured:
        verdict = "UNMEASURED"
    series = {
        name: {"unit": unit, **nearest_rank_summary(series_samples[name])}
        for name, unit in SERIES_FIELDS.items()
    }
    if provenance == "hardware":
        for summary in series.values():
            if summary["sample_count"] > 0:
                summary["provenance"] = "hardware"
    missing = [name for name, summary in series.items() if summary["sample_count"] == 0]
    if not marker_ok:
        missing.append("post_reset_success_marker")
    status = "COMPLETE" if marker_ok and verdict == "PASS" and not missing else "INCOMPLETE"
    if verdict == "PASS" and missing:
        status = "INCOMPLETE"
    return {
        "document": DOCUMENT,
        "run_id": run_id,
        "schema_version": SCHEMA_VERSION,
        "parser_version": PARSER_VERSION,
        "evidence_provenance": provenance,
        "provenance_context": provenance_context,
        "evidence": {
            "status": status,
            "trace_loss": "UNKNOWN",
            "firmware_trace_loss": {
                "status": "UNKNOWN",
                "dropped_events": None,
                "oversized_events": None,
                "malformed_rows": None,
            },
            "missing": missing,
        },
        "lab_metrics": {
            "experiment_inputs": list(EXPERIMENT_INPUTS),
            "experiment_verdict": verdict,
            "control": control,
            "scenarios": scenarios,
            "series": series,
        },
    }


def parse_file(
    serial_path: Path,
    output_path: Path | None = None,
    *,
    expected_run_id: str | None = None,
    expected_mode: str = "manual",
    expected_variant: str | None = None,
    post_reset: bool = True,
    capture_context: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    data = Path(serial_path).read_bytes()
    if len(data) > MAX_INPUT_BYTES:
        raise ParseError("serial input exceeds byte limit")
    try:
        decoded = data.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise ParseError("serial is not valid UTF-8") from exc
    run_id = expected_run_id or Path(serial_path).parent.name
    output = parse(
        decoded,
        run_id,
        mode=expected_mode,
        variant=expected_variant,
        post_reset=post_reset,
        capture_context=capture_context,
    )
    if output_path is not None:
        destination = Path(output_path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(
            json.dumps(output, indent=2, allow_nan=False) + "\n",
            encoding="utf-8",
        )
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description="Parse a LAB-05 peak serial log.")
    parser.add_argument("serial", type=Path)
    parser.add_argument("-o", "--out", required=True, type=Path)
    parser.add_argument("--mode", choices=("manual", "scripted"), default="manual")
    parser.add_argument("--variant")
    args = parser.parse_args()
    parse_file(args.serial, args.out, expected_mode=args.mode, expected_variant=args.variant)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
