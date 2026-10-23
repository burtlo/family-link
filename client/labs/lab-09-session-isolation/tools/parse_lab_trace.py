#!/usr/bin/env python3
"""LAB-09 trace parser. Nearest-rank percentiles are local to this file."""
from __future__ import annotations

import argparse
import math
import re
from pathlib import Path
from typing import Any

PARSER_DOCUMENT = "family-link.lab-metrics/v2"
TRACE_SCHEMA = "lab09-trace/v1"
PARSER_VERSION = "lab09-parser/v1"
EXPERIMENT_INPUTS = ["I-mute", "I-signout", "I-switch", "I-push", "I-expire-client"]
EXPLORATORY = ("I-confused", "I-revoke-branches", "I-expire-http")
KINDS = {"inbox", "media", "playback", "profile"}
_KV = re.compile(r"([a-z][a-z0-9_]*)=([^\s]*)")
_SECRET = re.compile(r"(?i)(?:\bpassword\s*[=:]\s*\S+|\bbearer\s+\S+|\"handle\"\s*:\s*\"[^\"]+\")")
_INTS = {
    "seq", "monotonic_us", "operation_id", "generation", "session_generation", "terminal_count",
    "applied", "image_bytes", "card_count", "duplicate_cards", "display_power", "http_status",
    "b_side_effects", "stage_restored", "update_level", "elapsed_us", "revoked",
    "internal_largest_free_block_bytes", "psram_largest_free_block_bytes",
    "lvgl_largest_free_block_bytes", "dropped_events", "oversized_events", "malformed_rows",
}
_LOSS_FIELDS = {"schema", "event", "dropped_events", "oversized_events", "malformed_rows"}


class ParseError(ValueError):
    """Input cannot be trusted as LAB-09 evidence."""


def redact_line(line: str) -> str:
    line = re.sub(r"(?i)https?://[^\s\"']+", "[REDACTED_URL]", line)
    line = re.sub(r"(?i)(password|passwd)\s*[=:]\s*[^\s,;]+", r"\1=[REDACTED]", line)
    line = re.sub(r"(?i)Bearer\s+\S+", "Bearer [REDACTED]", line)
    line = re.sub(r'"(handle|credential|token)"\s*:\s*"[^"]*"', r'"\1":"[REDACTED]"', line)
    return line


def validate_evidence_line(line: str) -> None:
    if _SECRET.search(line):
        raise ParseError("serial line contains a raw credential")


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
    body = line[len("LAB09_TRACE "):].strip()
    matches = list(_KV.finditer(body))
    if not matches or " ".join(match.group(0) for match in matches) != body:
        raise ParseError("malformed trace key/value row")
    result: dict[str, str] = {}
    for match in matches:
        key, value = match.groups()
        if key in result:
            raise ParseError(f"duplicate trace field: {key}")
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
    if scenario == "I-mute":
        return (row.get("terminal") in {"stale", "cancelled"} and row.get("applied") == 0
                and row.get("image_bytes") == 0 and row.get("display_power") == 0
                and row.get("stage") == "muted" and row.get("kind") in KINDS)
    if scenario == "I-signout":
        return (row.get("terminal") in {"stale", "cancelled"} and row.get("applied") == 0
                and row.get("stage") == "signed_out" and row.get("image_bytes") == 0)
    if scenario == "I-switch":
        return (row.get("applied") == 0 and row.get("image_bytes") == 0
                and row.get("stage") == "signed_in" and row.get("terminal") in {"stale", "cancelled"}
                and row.get("generation") != row.get("session_generation"))
    if scenario == "I-push":
        return (row.get("terminal") == "success" and row.get("applied") == 1
                and row.get("duplicate_cards") == 0 and row.get("card_count") == 12
                and row.get("kind") == "inbox")
    if scenario == "I-expire-client":
        return (row.get("applied") == 0 and row.get("terminal") != "success"
                and row.get("stage_restored") == 0 and row.get("terminal_count") == 1
                and row.get("image_bytes") == 0)
    return False


def evaluate_experiment(scenarios: dict[str, Any], experiment_inputs: list[str]) -> str:
    """Workflow success is not an input. An empty decided set is unmeasured."""
    if len(experiment_inputs) == 0:
        return "UNMEASURED"
    failed = False
    missing = False
    for scenario_id in experiment_inputs:
        scenario = scenarios.get(scenario_id)
        samples = [] if not scenario else list(scenario.get("samples") or [])
        if not samples:
            missing = True
            continue
        seen = []
        kinds = set()
        for row in samples:
            if not _scenario_ok(scenario_id, row):
                failed = True
            operation_id = row.get("operation_id")
            if operation_id in seen:
                failed = True
            seen.append(operation_id)
            if row.get("kind"):
                kinds.add(row.get("kind"))
        if scenario_id == "I-mute" and kinds != KINDS:
            failed = True
        if scenario_id != "I-mute" and len(samples) != 1:
            failed = True
    if failed:
        return "FAIL"
    if missing:
        return "INCOMPLETE"
    return "PASS"


def _firmware_trace_loss(row: dict[str, Any] | None) -> tuple[str, dict[str, Any]]:
    if row is None:
        return "UNKNOWN", {
            "status": "UNKNOWN",
            "dropped_events": None,
            "oversized_events": None,
            "malformed_rows": None,
        }
    if set(row) != _LOSS_FIELDS:
        raise ParseError("malformed firmware trace loss row")
    dropped = row["dropped_events"]
    oversized = row["oversized_events"]
    malformed = row["malformed_rows"]
    status = "NONE" if dropped == oversized == malformed == 0 else "OBSERVED"
    return status, {
        "status": status,
        "dropped_events": dropped,
        "oversized_events": oversized,
        "malformed_rows": malformed,
    }


def _observation(row: dict[str, Any]) -> dict[str, Any]:
    return {
        "terminal": row.get("terminal"),
        "stage": row.get("stage"),
        "terminal_count": row.get("terminal_count"),
        "applied": row.get("applied"),
        "image_bytes": row.get("image_bytes"),
        "http_status": row.get("http_status"),
        "b_side_effects": row.get("b_side_effects"),
        "reason": row.get("reason"),
        "display_power": row.get("display_power"),
    }


def parse(serial: str, run_id: str, mode: str = "scripted", variant: str | None = None,
          capture_context: dict[str, Any] | None = None, post_reset: bool = False,
          experiment_inputs: list[str] | None = None) -> dict[str, Any]:
    del variant, post_reset
    validate_evidence_line(serial)
    provenance = "host"
    if capture_context and capture_context.get("kind") == "device_serial_capture":
        provenance = "hardware"
    scenarios: dict[str, Any] = {}
    cancels: list[float] = []
    internal_free: list[float] = []
    psram_free: list[float] = []
    lvgl_free: list[float] = []
    loss_row: dict[str, Any] | None = None
    for line in serial.splitlines():
        if not line.startswith("LAB09_TRACE "):
            continue
        validate_evidence_line(line)
        fields = _coerce(_fields(line))
        if fields.get("schema") != TRACE_SCHEMA:
            raise ParseError("wrong trace schema")
        event = fields.get("event")
        if event == "trace_loss":
            if loss_row is not None:
                raise ParseError("duplicate firmware trace loss row")
            loss_row = fields
            continue
        if event == "resource_sample":
            for key, bucket in (
                ("internal_largest_free_block_bytes", internal_free),
                ("psram_largest_free_block_bytes", psram_free),
                ("lvgl_largest_free_block_bytes", lvgl_free),
            ):
                if key in fields:
                    bucket.append(fields[key])
            continue
        if event == "cancel_sample" and "elapsed_us" in fields:
            cancels.append(fields["elapsed_us"])
            continue
        if event == "observation":
            scenario = str(fields.get("scenario", ""))
            if scenario not in EXPLORATORY:
                continue
            bucket = scenarios.setdefault(scenario, {
                "class": "exploratory",
                "sample_count": 0,
                "samples": [],
                "observation": {"terminal": "recorded"},
            })
            bucket["class"] = "exploratory"
            bucket["samples"].append(fields)
            bucket["sample_count"] = len(bucket["samples"])
            observation = bucket["observation"]
            observation["terminal"] = fields.get("terminal", "recorded")
            if "b_side_effects" in fields:
                observation["b_side_effects"] = fields["b_side_effects"]
            if "http_status" in fields and scenario == "I-expire-http":
                observation["http_status"] = fields["http_status"]
            mode_name = fields.get("mode")
            if scenario == "I-revoke-branches" and mode_name and mode_name != "none":
                modes = observation.setdefault("modes", {})
                modes[mode_name] = {"revoked": fields.get("revoked")}
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
    trace_loss, firmware_loss = _firmware_trace_loss(loss_row)
    sample_provenance = provenance if provenance == "hardware" else "host"
    measurements = {
        "mute_cancel_us": _series(cancels, "us", sample_provenance if cancels else "unmeasured/blocked"),
        "internal_largest_free_block_bytes": _series(
            internal_free, "bytes", sample_provenance if internal_free else "unmeasured/blocked"),
        "psram_largest_free_block_bytes": _series(
            psram_free, "bytes", sample_provenance if psram_free else "unmeasured/blocked"),
        "lvgl_largest_free_block_bytes": _series(
            lvgl_free, "bytes", sample_provenance if lvgl_free else "unmeasured/blocked"),
    }
    return {
        "document": PARSER_DOCUMENT,
        "run_id": run_id,
        "schema_version": TRACE_SCHEMA,
        "parser_version": PARSER_VERSION,
        "mode": mode,
        "evidence_provenance": sample_provenance if sample_provenance == "hardware" else "host",
        "evidence": {
            "status": "COMPLETE" if verdict == "PASS" else "INCOMPLETE",
            "trace_loss": trace_loss,
            "firmware_trace_loss": firmware_loss,
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
        Path(output_path).write_text(__import__("json").dumps(result, indent=2) + "\n", encoding="utf-8")
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
