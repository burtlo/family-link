"""LAB-11 trace parser. Metric names belong to this lab."""
from __future__ import annotations

import json
import math
import re
from pathlib import Path
from typing import Any

PARSER_DOCUMENT = "family-link.lab-metrics/v2"
TRACE_SCHEMA = "lab11-trace/v1"
PARSER_VERSION = "lab11-parser/v1"
EXPERIMENT_INPUTS = ["D-one", "D-drop-ack", "D-drop-receipt", "D-full", "D-cancel"]
EXPLORATORY = ["D-many", "D-invalid-recipient", "D-offline", "D-push-fail", "D-restart"]
SECRET = re.compile(r"(pin\s*=|password\s*=|Bearer\s+\S|payload=|payload_base64)", re.IGNORECASE)


class ParseError(ValueError):
    """The serial fixture is not usable evidence."""


def nearest_rank(samples: list[float], percentile: float) -> float:
    ordered = sorted(samples)
    rank = math.ceil(percentile * len(ordered) / 100.0)
    rank = min(max(rank, 1), len(ordered))
    return ordered[rank - 1]


def _series(samples: list[float], unit: str, provenance: str, missing_reason: str) -> dict[str, Any]:
    if provenance == "hardware":
        provenance = "unmeasured/blocked"
    if not samples:
        return {
            "unit": unit,
            "sample_count": 0,
            "p50": None,
            "p95": None,
            "p99": None,
            "worst": None,
            "max": None,
            "provenance": provenance,
            "missing_reason": missing_reason,
        }
    return {
        "unit": unit,
        "sample_count": len(samples),
        "p50": nearest_rank(samples, 50),
        "p95": nearest_rank(samples, 95),
        "p99": nearest_rank(samples, 99),
        "worst": max(samples),
        "max": max(samples),
        "provenance": provenance,
        "missing_reason": None,
    }


def _fields(line: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for token in line.split()[1:]:
        if "=" not in token:
            raise ParseError("malformed trace token")
        key, value = token.split("=", 1)
        if not key or key in fields:
            raise ParseError("malformed or duplicate trace field")
        fields[key] = value
    return fields


def _need(fields: dict[str, str], key: str) -> str:
    if key not in fields:
        raise ParseError(f"missing trace field {key}")
    return fields[key]


def _int(fields: dict[str, str], key: str) -> int:
    try:
        return int(_need(fields, key), 10)
    except ValueError as exc:
        raise ParseError(f"malformed integer {key}") from exc


def _observation(fields: dict[str, str]) -> dict[str, Any]:
    observation: dict[str, Any] = {
        "terminal": fields.get("terminal"),
        "terminal_count": int(fields["terminal_count"]) if "terminal_count" in fields else None,
        "accepted": int(fields["accepted"]) if "accepted" in fields else None,
        "reason": fields.get("reason"),
        "condition": fields.get("condition"),
        "work": fields.get("work"),
        "push_result": fields.get("push_result"),
        "restart_status": fields.get("restart_status"),
        "resumed_sequences": fields.get("resumed_sequences"),
        "recipients": int(fields["recipients"]) if "recipients" in fields else None,
        "identity_conflict": int(fields["identity_conflict"]) if "identity_conflict" in fields else None,
        "lookup_same": int(fields["lookup_same"]) if "lookup_same" in fields else None,
        "complete_posts": int(fields["complete_posts"]) if "complete_posts" in fields else None,
        "remote_rollback": int(fields["remote_rollback"]) if "remote_rollback" in fields else None,
        "cancel_phase": fields.get("cancel_phase"),
        "live_buffers": int(fields["live_buffers"]) if "live_buffers" in fields else None,
        "chunk_bytes": int(fields["chunk_bytes"]) if "chunk_bytes" in fields else None,
        "operation_id": int(fields["operation_id"]) if "operation_id" in fields else None,
    }
    return observation


def _decided_ok(scenario: str, samples: list[dict[str, str]]) -> bool | None:
    """True pass, False violation, None incomplete."""
    if not samples:
        return None
    if scenario == "D-one":
        conditions = {row.get("condition") for row in samples}
        if "accept-on-archive-commit" not in conditions or "accept-on-recipient-publish" not in conditions:
            return None
        for row in samples:
            success = row.get("terminal") == "success"
            accepted = row.get("accepted") == "1"
            if success != accepted or row.get("terminal_count") != "1":
                return False
        return True
    if scenario == "D-drop-ack":
        row = samples[-1]
        if row.get("identity_conflict") != "1" or int(row.get("retry_same_sequence", "0")) < 1:
            return False
        if row.get("terminal") != "success" or row.get("accepted") != "1":
            return False
        return row.get("terminal_count") == "1"
    if scenario == "D-drop-receipt":
        row = samples[-1]
        return (row.get("lookup_same") == "1" and row.get("accepted") == "1"
                and row.get("complete_posts") == "1" and row.get("terminal") == "success"
                and row.get("terminal_count") == "1")
    if scenario == "D-full":
        if len(samples) != 1:
            return False
        row = samples[0]
        return (row.get("terminal") == "failure" and row.get("reason") == "storage"
                and row.get("terminal_count") == "1" and row.get("live_buffers") == "0"
                and row.get("sketch_len") == "0" and row.get("pcm_len") == "0"
                and row.get("accepted") == "0")
    if scenario == "D-cancel":
        if len(samples) != 4:
            return None
        ids = [row.get("operation_id") for row in samples]
        if len(set(ids)) != 4:
            return False
        phases = {row.get("cancel_phase"): row for row in samples}
        required = ("before_byte", "mid_upload", "after_accept", "during_lookup")
        if any(name not in phases for name in required):
            return None
        for name in ("before_byte", "mid_upload"):
            row = phases[name]
            if row.get("terminal") != "cancelled" or row.get("accepted") != "0" or row.get("terminal_count") != "1":
                return False
        after = phases["after_accept"]
        if after.get("terminal_count") != "1" or after.get("accepted") != "1" or after.get("remote_rollback") != "0":
            return False
        lookup = phases["during_lookup"]
        if (lookup.get("terminal") != "cancelled" or lookup.get("accepted") != "1"
                or lookup.get("terminal_count") != "1" or lookup.get("remote_rollback") != "0"
                or lookup.get("lookup_same") != "1"):
            return False
        return True
    return False


def evaluate_experiment(scenarios: dict[str, Any], experiment_inputs: list[str]) -> str:
    if not experiment_inputs:
        return "UNMEASURED"
    saw_incomplete = False
    for scenario_id in experiment_inputs:
        row = scenarios.get(scenario_id)
        if row is None or row.get("class") != "decided":
            saw_incomplete = True
            continue
        verdict = _decided_ok(scenario_id, row.get("samples") or [])
        if verdict is False:
            return "FAIL"
        if verdict is None:
            saw_incomplete = True
    if saw_incomplete:
        return "INCOMPLETE"
    return "PASS"


def parse(serial: str, run_id: str, mode: str = "scripted", variant: str | None = None,
          capture_context: dict[str, Any] | None = None, post_reset: bool = False,
          experiment_inputs: list[str] | None = None) -> dict[str, Any]:
    del variant, post_reset
    if SECRET.search(serial):
        raise ParseError("serial fixture contains a secret or raw payload")
    scenarios: dict[str, Any] = {}
    accounts: list[dict[str, Any]] = []
    ledgers: list[dict[str, Any]] = []
    seen_ops: set[str] = set()
    for line in serial.splitlines():
        if not line.startswith("LAB11_TRACE "):
            continue
        fields = _fields(line)
        if fields.get("schema") != TRACE_SCHEMA:
            raise ParseError("wrong or missing trace schema")
        if "mode" in fields and fields["mode"] != mode:
            raise ParseError("variant/phase mismatch")
        event = _need(fields, "event")
        if event == "part_overflow":
            if fields.get("dropped") != "0" or fields.get("queue_count") != "8":
                raise ParseError("overflow dropped or did not preserve the queue count")
            continue
        if event == "resource_account":
            accounts.append({key: fields.get(key) for key in (
                "chunk_bytes", "retained_input_bytes", "staging_bytes", "transport_bytes",
                "transport_view_bytes", "retry_extra_bytes", "response_bytes",
                "full_payload_duplicate_bytes", "peak_calculated_bytes", "provenance")})
            continue
        if event == "resource_ledger":
            ledgers.append({
                "ledger": fields.get("ledger"),
                "p50": None,
                "p95": None,
                "p99": None,
                "worst": None,
                "largest_free_block_bytes": None,
                "provenance": "unmeasured/blocked",
            })
            continue
        if event != "scenario_terminal":
            continue
        if fields.get("phase") == "checking" and fields.get("terminal") in (None, "none"):
            raise ParseError("checking phase remained without a terminal")
        operation_id = _need(fields, "operation_id")
        _need(fields, "terminal")
        _need(fields, "terminal_count")
        scenario = _need(fields, "scenario")
        if operation_id in seen_ops:
            raise ParseError("duplicate terminal for one operation id")
        seen_ops.add(operation_id)
        if scenario in EXPERIMENT_INPUTS:
            cls = "decided"
        elif scenario in EXPLORATORY:
            cls = "exploratory"
        else:
            raise ParseError("unknown scenario")
        if fields.get("class") not in (None, cls):
            raise ParseError("variant/phase mismatch")
        bucket = scenarios.setdefault(scenario, {"class": cls, "sample_count": 0, "samples": [], "observation": {}})
        bucket["samples"].append(fields)
        bucket["sample_count"] = len(bucket["samples"])
        bucket["observation"] = _observation(fields)
        bucket["terminal_count"] = fields.get("terminal_count")
    if experiment_inputs is None:
        experiment_inputs = list(EXPERIMENT_INPUTS)
    else:
        experiment_inputs = list(experiment_inputs)
    verdict = evaluate_experiment(scenarios, experiment_inputs)
    for scenario_id in EXPLORATORY:
        row = scenarios.get(scenario_id)
        if row is not None and "observation" not in row:
            raise ParseError("exploratory scenario missing observation")
    measurements = {
        "part_time_us": _series([], "us", "unmeasured/blocked", "device part timing was not measured"),
        "lvgl_largest_free_block_bytes": _series(
            [], "bytes", "unmeasured/blocked", "LVGL pool was not measured on device"),
        "transient_largest_free_block_bytes": _series(
            [], "bytes", "unmeasured/blocked", "transient buffers were not measured on device"),
    }
    return {
        "document": PARSER_DOCUMENT,
        "run_id": run_id,
        "schema_version": TRACE_SCHEMA,
        "parser_version": PARSER_VERSION,
        "mode": mode,
        "evidence_provenance": "host",
        "evidence": {
            "status": "COMPLETE" if verdict == "PASS" else "INCOMPLETE",
            "firmware_trace_loss": {"status": "UNKNOWN", "dropped_events": None},
        },
        "lab_metrics": {
            "experiment_inputs": experiment_inputs,
            "experiment_verdict": verdict,
            "performance_verdict": "UNMEASURED",
            "scenarios": scenarios,
            "resource_accounts": accounts,
            "resource_ledgers": ledgers,
            "measurements": measurements,
        },
    }


def parse_file(path: Path, output_path: Path | None = None, expected_run_id: str = "",
               expected_mode: str = "scripted", post_reset: bool = False,
               capture_context: dict[str, Any] | None = None) -> dict[str, Any]:
    text = Path(path).read_text(encoding="utf-8")
    result = parse(text, expected_run_id or "", mode=expected_mode, post_reset=post_reset,
                   capture_context=capture_context)
    if output_path is not None:
        Path(output_path).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result
