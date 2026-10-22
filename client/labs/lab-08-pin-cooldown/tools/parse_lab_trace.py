#!/usr/bin/env python3
"""LAB-08 trace parser. Nearest-rank percentiles are local to this file."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path
from typing import Any

PARSER_DOCUMENT = "family-link.lab-metrics/v2"
TRACE_SCHEMA = "lab08-trace/v1"
PARSER_VERSION = "lab08-parser/v1"
EXPERIMENT_INPUTS = [
    "K-ok",
    "K-wrong",
    "K-five",
    "K-switch-user",
    "K-timeouts",
    "K-lost",
    "K-mute-local",
    "K-boot",
    "K-idle",
]
EXPLORATORY = ("K-concurrent", "K-mismatch", "K-mute-server")
_KV = re.compile(r"([a-z][a-z0-9_]*)=([^\s]*)")
_RAW_SECRET = re.compile(
    r"(?i)(?:(?<![0-9a-fA-F])(?:1234|5678|9999|1111)(?![0-9a-fA-F])|"
    r"\bpin\s*[=:]\s*\d|"
    r"\b(?:password|passwd)\s*[=:]\s*\S+|"
    r"\bbearer\s+\S+)"
)
_PIN_DIGESTS = {
    hashlib.sha256(pin.encode("ascii")).hexdigest()
    for pin in ("1234", "5678", "9999", "1111")
}
_INTS = {
    "seq", "monotonic_us", "operation_id", "generation", "key_generation", "terminal_count",
    "attempt", "digit_len", "display_power", "wrong_count", "cooldown_remaining_ms",
    "side_effects", "delay_ms_0", "delay_ms_1", "delay_ms_2", "attempts_used", "boot_zero",
    "boot_partial", "boot_full", "update_level", "elapsed_us",
    "internal_largest_free_block_bytes", "psram_largest_free_block_bytes",
    "lvgl_largest_free_block_bytes", "dropped_events", "oversized_events", "malformed_rows",
}
_LOSS_FIELDS = {"schema", "event", "dropped_events", "oversized_events", "malformed_rows"}


class ParseError(ValueError):
    """Input cannot be trusted as LAB-08 evidence."""


def redact_line(line: str) -> str:
    line = re.sub(r"(?i)https?://[^\s\"']+", "[REDACTED_URL]", line)
    line = re.sub(r"(?i)(password|passwd)\s*[=:]\s*[^\s,;]+", r"\1=[REDACTED]", line)
    line = re.sub(r"(?i)Bearer\s+[A-Za-z0-9._~+/=-]+", "Bearer [REDACTED]", line)
    return line


def validate_evidence_line(line: str) -> None:
    if _RAW_SECRET.search(line):
        raise ParseError("serial line contains a raw PIN or credential")
    for digest in _PIN_DIGESTS:
        if digest in line:
            raise ParseError("serial line contains a hash of a four-digit PIN")


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
    body = line[len("LAB08_TRACE "):].strip()
    matches = list(_KV.finditer(body))
    if not matches or " ".join(match.group(0) for match in matches) != body:
        raise ParseError("malformed trace key/value row")
    result: dict[str, str] = {}
    for match in matches:
        key, value = match.groups()
        if key in result:
            raise ParseError(f"duplicate trace field: {key}")
        result[key] = value
    fingerprint = result.get("key_fingerprint", "")
    if fingerprint in _PIN_DIGESTS:
        raise ParseError("key fingerprint is a hash of a four-digit PIN")
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
    if scenario == "K-ok":
        return row.get("terminal") == "success" and row.get("side_effects") == 1 and row.get("display_power") == 1
    if scenario == "K-wrong":
        return (row.get("terminal") == "failure" and row.get("pin_copy") == "wrong_pin"
                and row.get("reason") == "credential" and row.get("wrong_count") == 1)
    if scenario == "K-five":
        return (row.get("terminal") == "failure" and row.get("pin_copy") == "cooldown"
                and row.get("wrong_count") == 5 and row.get("cooldown_remaining_ms") == 60000)
    if scenario == "K-switch-user":
        return row.get("terminal") == "cancelled" and row.get("wrong_count") == 0
    if scenario == "K-timeouts":
        return (row.get("terminal") == "timed_out" and row.get("stage") == "connecting"
                and row.get("wrong_count") == 0 and row.get("delay_ms_0") == 1000
                and row.get("delay_ms_1") == 2000 and row.get("delay_ms_2") == 4000
                and row.get("attempts_used") == 4)
    if scenario == "K-lost":
        return row.get("terminal") == "success" and row.get("side_effects") == 1
    if scenario == "K-mute-local":
        return (row.get("terminal") in {"cancelled", "stale"} and row.get("display_power") == 0
                and row.get("digit_len") == 0 and row.get("stage") == "muted")
    if scenario == "K-boot":
        return row.get("boot_zero") == 1 and row.get("boot_partial") == 1 and row.get("boot_full") == 1
    if scenario == "K-idle":
        return (row.get("terminal") == "cancelled" and row.get("digit_len") == 0
                and row.get("stage") == "roster")
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
        "side_effects": row.get("side_effects"),
        "reason": row.get("reason"),
        "display_power": row.get("display_power"),
        "digit_len": row.get("digit_len"),
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
    callbacks: list[float] = []
    internal_free: list[float] = []
    psram_free: list[float] = []
    lvgl_free: list[float] = []
    loss_row: dict[str, Any] | None = None
    for line in serial.splitlines():
        if not line.startswith("LAB08_TRACE "):
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
        if event == "input_callback" and "elapsed_us" in fields:
            callbacks.append(fields["elapsed_us"])
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
        "input_callback_us": _series(callbacks, "us", sample_provenance if callbacks else "unmeasured/blocked"),
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
