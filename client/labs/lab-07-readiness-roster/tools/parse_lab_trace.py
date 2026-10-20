#!/usr/bin/env python3
"""LAB-07 trace parser. Nearest-rank percentiles are local to this file."""
from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path
from typing import Any

PARSER_DOCUMENT = "family-link.lab-metrics/v2"
TRACE_SCHEMA = "lab07-trace/v1"
PARSER_VERSION = "lab07-parser/v1"
EXPERIMENT_INPUTS = [
    "R-wifi-absent",
    "R-invalid-200",
    "R-empty",
    "R-eight",
    "R-retry",
    "R-retry-fail",
    "R-idle",
    "R-stale-beat",
    "R-mute",
    "R-timeout-ui",
]
EXPLORATORY = ("R-wire", "R-stale-clock")
_KV = re.compile(r"([a-z][a-z0-9_]*)=([^\s]*)")
_PIN = re.compile(r"(?i)(?:\bpin\s*[=:]\s*\d|\bwrong_pin\s*[=:]|\b(?:password|passwd)\s*[=:]\s*\S+|\bbearer\s+\S+)")
_INTS = {
    "seq", "monotonic_us", "operation_id", "generation", "terminal_count", "attempt",
    "user_count", "selectable_cards", "focus_stable", "stale", "missed_beats",
    "inflight_requests", "touch_during_wait", "queue_count", "roster_period_ms",
    "heartbeat_period_ms", "valid", "stage_unchanged", "grace_ms", "clear_stale",
    "elapsed_ms", "request_timeout_ms", "lvgl_high_water_bytes",
    "lvgl_largest_free_block_bytes", "pixel_high_water_bytes", "pixel_largest_free_block_bytes",
}


class ParseError(ValueError):
    """Input cannot be trusted as LAB-07 evidence."""


def redact_line(line: str) -> str:
    line = re.sub(r"(?i)https?://[^\s\"']+", "[REDACTED_URL]", line)
    line = re.sub(r"(?i)(password|passwd)\s*[=:]\s*[^\s,;]+", r"\1=[REDACTED]", line)
    line = re.sub(r"(?i)Bearer\s+[A-Za-z0-9._~+/=-]+", "Bearer [REDACTED]", line)
    return line


def validate_evidence_line(line: str) -> None:
    if _PIN.search(line):
        raise ParseError("serial line contains a raw PIN or credential")


def nearest_rank(samples: list[float], percent: int) -> float | None:
    finite = sorted(value for value in samples if isinstance(value, (int, float)) and math.isfinite(value))
    count = len(finite)
    if count == 0:
        return None
    rank = math.ceil(percent * count / 100)
    if rank < 1:
        rank = 1
    if rank > count:
        rank = count
    return finite[rank - 1]


def _series(samples: list[float], unit: str, provenance: str) -> dict[str, Any]:
    finite = [value for value in samples if isinstance(value, (int, float)) and math.isfinite(value)]
    if not finite:
        return {
            "sample_count": 0,
            "unit": unit,
            "p50": None,
            "p95": None,
            "p99": None,
            "max": None,
            "provenance": "unmeasured/blocked",
            "missing_reason": "no eligible samples in trace",
        }
    return {
        "sample_count": len(finite),
        "unit": unit,
        "p50": nearest_rank(finite, 50),
        "p95": nearest_rank(finite, 95),
        "p99": nearest_rank(finite, 99),
        "max": max(finite),
        "provenance": provenance,
        "missing_reason": None,
    }


def _fields(line: str) -> dict[str, str]:
    body = line[len("LAB07_TRACE "):].strip()
    matches = list(_KV.finditer(body))
    if not matches or " ".join(match.group(0) for match in matches) != body:
        raise ParseError("malformed trace key/value row")
    result: dict[str, str] = {}
    for match in matches:
        key, value = match.groups()
        if key in result:
            raise ParseError(f"duplicate trace field: {key}")
        if key == "wrong_pin":
            raise ParseError("wrong_pin is not a LAB-07 field")
        result[key] = value
    return result


def _coerce(fields: dict[str, str]) -> dict[str, Any]:
    row: dict[str, Any] = {}
    for key, value in fields.items():
        if key in _INTS:
            if not re.fullmatch(r"0|[1-9][0-9]*", value):
                raise ParseError(f"invalid integer {key}")
            row[key] = int(value)
        else:
            row[key] = value
    return row


def _scenario_ok(scenario: str, row: dict[str, Any]) -> bool:
    if row.get("terminal_count") != 1:
        return False
    if scenario == "R-wifi-absent":
        return row.get("stage") == "wifi_unavailable" and row.get("selectable_cards") == 0
    if scenario == "R-invalid-200":
        return row.get("valid") == 0 and row.get("selectable_cards") == 0 and row.get("stage") == "connecting"
    if scenario == "R-empty":
        return row.get("stage") == "empty_hangout"
    if scenario == "R-eight":
        return row.get("user_count") == 8 and row.get("focus_stable") == 1
    if scenario == "R-retry":
        return row.get("attempt") == 2 and row.get("stage") == "roster" and row.get("stage_unchanged") == 1
    if scenario == "R-retry-fail":
        return row.get("stage") == "connecting"
    if scenario == "R-idle":
        return row.get("roster_period_ms") == 300000 and row.get("heartbeat_period_ms") == 60000
    if scenario == "R-stale-beat":
        return row.get("stale") == 1 and row.get("stage_unchanged") == 1 and row.get("missed_beats") == 3
    if scenario == "R-mute":
        return (row.get("terminal") == "stale" and row.get("stage") == "muted"
                and row.get("selectable_cards") == 0 and row.get("inflight_requests") == 0)
    if scenario == "R-timeout-ui":
        return (row.get("terminal") == "timed_out" and row.get("touch_during_wait") == 1
                and "wrong_pin" not in row)
    return False


def evaluate_experiment(scenarios: dict[str, Any], experiment_inputs: list[str]) -> str:
    """Workflow success is not an input. An empty decided set is unmeasured."""
    if len(experiment_inputs) == 0:
        return "UNMEASURED"
    failed = False
    missing = False
    for scenario_id in experiment_inputs:
        scenario = scenarios.get(scenario_id)
        if not scenario or scenario.get("sample_count", 0) != 1:
            missing = True
            if scenario and scenario.get("sample_count", 0) > 1:
                failed = True
            continue
        row = scenario["samples"][0]
        if not _scenario_ok(scenario_id, row):
            failed = True
    if failed:
        return "FAIL"
    if missing:
        return "INCOMPLETE"
    return "PASS"


def _observation(row: dict[str, Any]) -> dict[str, Any]:
    observation = {
        "terminal": row.get("terminal"),
        "stage": row.get("stage"),
        "terminal_count": row.get("terminal_count"),
    }
    if row.get("scenario") == "R-wire":
        keys = str(row.get("parsed_keys", "")).split("|") if row.get("parsed_keys") not in (None, "none") else []
        observation["parsed_keys"] = keys
    if row.get("scenario") == "R-stale-clock":
        observation["grace_ms"] = row.get("grace_ms")
        observation["clear_stale"] = row.get("clear_stale")
    return observation


def parse(serial: str, run_id: str, mode: str = "scripted", variant: str | None = None,
          capture_context: dict[str, Any] | None = None, post_reset: bool = False,
          experiment_inputs: list[str] | None = None) -> dict[str, Any]:
    del variant, post_reset
    if _PIN.search(serial):
        raise ParseError("serial fixture contains a raw PIN or credential")
    provenance = "host"
    if capture_context and capture_context.get("kind") == "device_serial_capture":
        provenance = "hardware"
    scenarios: dict[str, Any] = {}
    elapsed: list[float] = []
    lvgl_free: list[float] = []
    lvgl_high: list[float] = []
    pixel_free: list[float] = []
    pixel_high: list[float] = []
    for line in serial.splitlines():
        if not line.startswith("LAB07_TRACE "):
            continue
        validate_evidence_line(line)
        fields = _coerce(_fields(line))
        if fields.get("schema") != TRACE_SCHEMA:
            raise ParseError("wrong trace schema")
        event = fields.get("event")
        if event == "resource_sample":
            for key, bucket in (
                ("lvgl_largest_free_block_bytes", lvgl_free),
                ("lvgl_high_water_bytes", lvgl_high),
                ("pixel_largest_free_block_bytes", pixel_free),
                ("pixel_high_water_bytes", pixel_high),
            ):
                if key in fields:
                    bucket.append(fields[key])
            continue
        if event == "device_timeout" and "elapsed_ms" in fields:
            elapsed.append(fields["elapsed_ms"])
            continue
        if event != "scenario_terminal":
            continue
        scenario = str(fields.get("scenario", ""))
        if scenario in EXPERIMENT_INPUTS:
            cls = "decided"
        elif scenario in EXPLORATORY:
            cls = "exploratory"
        else:
            continue
        bucket = scenarios.setdefault(scenario, {
            "class": cls,
            "sample_count": 0,
            "samples": [],
            "observation": {},
        })
        bucket["class"] = cls
        bucket["samples"].append(fields)
        bucket["sample_count"] = len(bucket["samples"])
        bucket["observation"] = _observation(fields)
        bucket["terminal_count"] = fields.get("terminal_count")
    if experiment_inputs is None:
        experiment_inputs = list(EXPERIMENT_INPUTS)
    else:
        experiment_inputs = list(experiment_inputs)
    verdict = evaluate_experiment(scenarios, experiment_inputs)
    measurements = {
        "elapsed_ms": _series(elapsed, "ms", provenance if elapsed else "unmeasured/blocked"),
        "lvgl_largest_free_block_bytes": _series(lvgl_free, "bytes", provenance if lvgl_free else "unmeasured/blocked"),
        "lvgl_high_water_bytes": _series(lvgl_high, "bytes", provenance if lvgl_high else "unmeasured/blocked"),
        "pixel_largest_free_block_bytes": _series(pixel_free, "bytes", provenance if pixel_free else "unmeasured/blocked"),
        "pixel_high_water_bytes": _series(pixel_high, "bytes", provenance if pixel_high else "unmeasured/blocked"),
    }
    return {
        "document": PARSER_DOCUMENT,
        "run_id": run_id,
        "schema_version": TRACE_SCHEMA,
        "parser_version": PARSER_VERSION,
        "mode": mode,
        "evidence_provenance": provenance if provenance == "hardware" else "host",
        "evidence": {
            "status": "COMPLETE" if verdict == "PASS" else "INCOMPLETE",
            "trace_loss": "UNKNOWN",
            "firmware_trace_loss": {"status": "UNKNOWN", "dropped_events": None},
            "missing": [] if verdict == "PASS" else ["decided_scenario"],
        },
        "lab_metrics": {
            "experiment_inputs": experiment_inputs,
            "experiment_verdict": verdict,
            "performance_verdict": "UNMEASURED",
            "scenarios": scenarios,
            "measurements": measurements,
        },
    }


def parse_file(path: Path, output_path: Path | None = None, expected_run_id: str = "",
               expected_mode: str = "scripted", expected_variant: str | None = None,
               post_reset: bool = False, capture_context: dict[str, Any] | None = None,
               expected_build_id: str = "") -> dict[str, Any]:
    del expected_variant, expected_build_id
    text = Path(path).read_text(encoding="utf-8")
    result = parse(text, expected_run_id, mode=expected_mode, capture_context=capture_context,
                   post_reset=post_reset)
    if output_path is not None:
        Path(output_path).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("serial", type=Path)
    parser.add_argument("-o", "--out", required=True, type=Path)
    parser.add_argument("--run-id", default="")
    parser.add_argument("--mode", choices=("manual", "scripted"), default="scripted")
    args = parser.parse_args()
    run_id = args.run_id or args.serial.parent.name or "unspecified"
    parse_file(args.serial, args.out, expected_run_id=run_id, expected_mode=args.mode)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
