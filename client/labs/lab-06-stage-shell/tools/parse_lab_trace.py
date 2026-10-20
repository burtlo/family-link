#!/usr/bin/env python3
"""Strict, bounded parser for LAB-06 JSON-lines serial evidence."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path
from typing import Any

DOCUMENT = "family-link.lab-metrics/v2"
TRACE_SCHEMA = "lab06-trace/v1"
PARSER_VERSION = "lab06-parser/v1"
MAX_INPUT_BYTES = 4 * 1024 * 1024
MAX_LINE_BYTES = 4096
MAX_LINES = 20000
MAX_ROWS = 4096
MAX_U32 = 2**32 - 1
DECIDED = ["H-cycle", "H-data", "H-data-during-exit", "H-level2-control", "H-destroyed", "H-cancel"]
EXPLORATORY = "H-placeholder"
STAGES = {"roster", "pin", "hangout", "inbox", "record", "receipt"}
EVENT_FIELDS = {
    "scenario_terminal": {"schema","event","scenario","class","operation_id","generation","stage_before","stage_after","terminal","terminal_count","reason","update_level","operation_count","queue_overflow","stale_handle_use","widget_live","timer_count","anim_us","build_us","placeholder","old_panel_text_visible"},
    "transition_terminal": {"schema","event","operation_id","generation","stage_before","stage_after","terminal","terminal_count","reason","update_level","widget_live","timer_count","anim_us"},
    "resource_sample": {"schema","event","reason","ledger_a","lvgl_total_bytes","lvgl_free_bytes","lvgl_largest_free_bytes","lvgl_high_water_pct","ledger_b","internal_free_bytes","internal_largest_free_bytes","psram_free_bytes","psram_largest_free_bytes","provenance"},
    "queue_overflow": {"schema","event","publication_id","queue_overflow","terminal","terminal_count","reason"},
    "trace_summary": {"schema","event","scenario","class","placeholder","experiment_inputs","scenario_pass_count","scenario_fail_count","operation_count","queue_overflow","stale_handle_use","peak_panel_widgets","timer_count","performance"},
}
_SECRET = re.compile(r"(?i)(?:\bpin\s*[=:]\s*\S+|\b(?:password|passwd)\s*[=:]\s*\S+|\bbearer\s+\S+|\b(?:media_payload|payload_b64|image_data|audio_data)\s*[=:])")
_EVIDENCE_BLOCKED = re.compile(r"(?i)(?:\bpin\s*[=:]\s*\S+|\b(?:media_payload|payload_b64|image_data|audio_data)\s*[=:])")


class ParseError(ValueError):
    pass


def validate_evidence_line(line: str) -> None:
    if _EVIDENCE_BLOCKED.search(line):
        raise ParseError("serial line contains a raw PIN or payload")


def _marker(line: str, prefix: str) -> dict[str, str]:
    out = {}
    for token in line[len(prefix):].split():
        if "=" not in token:
            raise ParseError("malformed lifecycle marker")
        key, value = token.split("=", 1)
        if not key or not value or key in out:
            raise ParseError("duplicate lifecycle marker field")
        out[key] = value
    return out


def _uint(row: dict, key: str, *, maximum: int = MAX_U32) -> int:
    value = row.get(key)
    if not isinstance(value, int) or isinstance(value, bool) or value < 0 or value > maximum:
        raise ParseError(f"invalid or overflowing {key}")
    return value


def _rows(text: str) -> list[dict[str, Any]]:
    data = text.encode("utf-8", errors="strict")
    if len(data) > MAX_INPUT_BYTES:
        raise ParseError("serial input exceeds byte bound")
    lines = text.splitlines()
    if len(lines) > MAX_LINES:
        raise ParseError("serial line count exceeds bound")
    found = []
    for line in lines:
        if len(line.encode("utf-8")) > MAX_LINE_BYTES:
            raise ParseError("serial line exceeds byte bound")
        if _SECRET.search(line):
            raise ParseError("serial evidence contains a secret or raw payload")
        if not line.startswith('{'):
            continue
        try:
            row = json.loads(line, object_pairs_hook=_unique_object)
        except (json.JSONDecodeError, ParseError) as exc:
            raise ParseError("malformed JSON trace row") from exc
        if not isinstance(row, dict) or row.get("schema") != TRACE_SCHEMA:
            raise ParseError("wrong trace schema or non-object row")
        event = row.get("event")
        if event not in EVENT_FIELDS:
            raise ParseError("unknown trace event")
        allowed = EVENT_FIELDS[event]
        if event == "queue_overflow":
            allowed = allowed | {"probe"}
        if set(row) != allowed:
            raise ParseError(f"malformed fields for {event}")
        if len(found) >= MAX_ROWS:
            raise ParseError("trace row count exceeds bound")
        found.append(row)
    return found


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ParseError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _validate_row(row):
    event = row["event"]
    if event == "scenario_terminal":
        if row["scenario"] not in {*DECIDED, EXPLORATORY}:
            raise ParseError("unknown scenario")
        if row["class"] != ("exploratory" if row["scenario"] == EXPLORATORY else "decided"):
            raise ParseError("scenario class mismatch")
        for key in ("operation_id", "generation", "operation_count", "queue_overflow", "stale_handle_use", "widget_live", "timer_count", "anim_us", "build_us"):
            _uint(row, key, maximum=2**64-1 if key == "operation_id" else MAX_U32)
        if row["operation_id"] == 0 or row["terminal_count"] != 1 or row["terminal"] not in {"success","failure","cancelled","stale","timed_out"}:
            raise ParseError("invalid scenario terminal")
        if row["stage_before"] not in STAGES or row["stage_after"] not in STAGES:
            raise ParseError("invalid stage")
        if row["update_level"] not in {0, 1, 2} or row["placeholder"] not in {0, 1} or row["old_panel_text_visible"] not in {0, 1}:
            raise ParseError("invalid scenario value")
        if (row["scenario"] == EXPLORATORY) != (row["placeholder"] == 1):
            raise ParseError("placeholder classification mismatch")
    elif event == "transition_terminal":
        for key in ("operation_id", "generation", "widget_live", "timer_count", "anim_us"):
            _uint(row, key, maximum=2**64-1 if key == "operation_id" else MAX_U32)
        if row["operation_id"] == 0 or row["terminal_count"] != 1 or row["terminal"] not in {"success","failure","cancelled","stale","timed_out"}:
            raise ParseError("invalid transition terminal")
        if row["stage_before"] not in STAGES or row["stage_after"] not in STAGES or row["update_level"] not in {0,1,2}:
            raise ParseError("invalid transition")
    elif event == "resource_sample":
        for key in ("lvgl_total_bytes","lvgl_free_bytes","lvgl_largest_free_bytes","lvgl_high_water_pct","internal_free_bytes","internal_largest_free_bytes","psram_free_bytes","psram_largest_free_bytes"):
            _uint(row,key,maximum=MAX_U32*1024)
        if row["ledger_a"] != "lvgl_pool" or row["ledger_b"] != "heap_caps" or row["lvgl_free_bytes"] > row["lvgl_total_bytes"] or row["lvgl_high_water_pct"] > 100:
            raise ParseError("invalid resource sample")
    elif event == "queue_overflow":
        for key in ("publication_id","queue_overflow"):
            _uint(row,key)
        if row["terminal"] != "failure" or row["terminal_count"] != 1 or row["reason"] != "overflow":
            raise ParseError("invalid queue overflow evidence")
    else:
        for key in ("placeholder","scenario_pass_count","scenario_fail_count","operation_count","queue_overflow","stale_handle_use","peak_panel_widgets","timer_count"):
            _uint(row,key,maximum=1 if key == "placeholder" else MAX_U32)
        if row["scenario"] != EXPLORATORY or row["class"] != "exploratory" or row["placeholder"] != 1 or row["performance"] != "UNMEASURED" or row["experiment_inputs"] != DECIDED:
            raise ParseError("invalid trace summary or experiment inventory")


def _nearest(values: list[int], pct: int) -> int | None:
    if not values:
        return None
    values = sorted(values)
    return values[max(0, math.ceil(pct * len(values) / 100) - 1)]


def _experiment_verdict(experiment_inputs: list[str], results: dict,
                        checks: dict[str, bool | None]) -> str:
    """Compute the decision from declared decided inputs only."""
    if not experiment_inputs or not any(results[name]["operation_results"] for name in experiment_inputs):
        return "UNMEASURED"
    complete = all(bool(results[name]["operation_results"]) for name in experiment_inputs)
    selected = [checks[name] for name in experiment_inputs]
    if any(value is False for value in selected):
        return "FAIL"
    if not complete or any(value is None for value in selected):
        return "INCOMPLETE"
    return "PASS"


def parse_text(text: str, *, run_id: str = "fixture", expected_mode: str | None = None,
               expected_variant: str | None = None, post_reset: bool = False,
               capture_context: dict | None = None, expected_build_id: str | None = None) -> dict:
    lines = text.splitlines()
    ready = [_marker(line, "LAB06_READY ") for line in lines if line.startswith("LAB06_READY ")]
    done = [_marker(line, "LAB06_HARNESS_DONE ") for line in lines if line.startswith("LAB06_HARNESS_DONE ")]
    # A serial marker describes the input; it cannot prove that the bytes came
    # from a reset-gated device capture. Only the adapter's complete capture
    # context can carry that fact into parsed evidence.
    required_capture = {"kind", "target_id", "post_reset", "reset_performed",
                        "ready_marker_verified", "build_fingerprint", "fixture_sha256"}
    trusted_capture = (post_reset is True and isinstance(capture_context, dict)
                       and set(capture_context) == required_capture
                       and capture_context.get("kind") == "device_serial_capture"
                       and capture_context.get("post_reset") is True
                       and capture_context.get("reset_performed") is True
                       and capture_context.get("ready_marker_verified") is True
                       and all(isinstance(capture_context.get(k), str) and capture_context[k]
                               for k in ("target_id", "build_fingerprint", "fixture_sha256")))
    if len(ready) != 1:
        raise ParseError("missing or duplicate post-reset LAB06_READY marker")
    if set(ready[0]) != {"build", "mode", "server", "trace_schema"} or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", ready[0].get("build", "")):
        raise ParseError("malformed LAB06_READY identity")
    if expected_mode is not None and ready[0].get("mode") != expected_mode:
        raise ParseError("LAB06_READY mode mismatch")
    if expected_variant is not None and expected_variant != expected_mode:
        raise ParseError("LAB-06 variant/phase mismatch")
    if ready[0].get("server") != "none" or ready[0].get("trace_schema") != TRACE_SCHEMA:
        raise ParseError("LAB06_READY identity mismatch")
    if expected_build_id is not None and ready[0].get("build") != expected_build_id:
        raise ParseError("LAB06_READY build identity mismatch")
    if expected_mode == "scripted" and (len(done) != 1 or done[0] != {"status":"complete"}):
        raise ParseError("missing, duplicate, or failed scripted harness marker")
    if any(line.startswith("LAB06_INIT_FAILED") or line.startswith("LAB06_RUNTIME_FAILED") for line in lines):
        raise ParseError("LAB-06 emitted a failure marker")
    rows = _rows(text)
    for row in rows:
        _validate_row(row)
    ids = [row["operation_id"] for row in rows if row["event"] in {"scenario_terminal","transition_terminal"}]
    if len(ids) != len(set(ids)):
        raise ParseError("duplicate operation id")
    scenarios = {name: {"class": "decided", "operation_results": []} for name in DECIDED}
    scenarios[EXPLORATORY] = {"class": "exploratory", "observation": {"placeholder": 1, "operation_results": []}}
    for row in rows:
        if row["event"] != "scenario_terminal":
            continue
        target = scenarios[row["scenario"]]
        out = {k: row[k] for k in ("operation_id","terminal","terminal_count","reason","update_level","stage_before","stage_after","widget_live","timer_count","queue_overflow","stale_handle_use","old_panel_text_visible","placeholder")}
        (target["observation"]["operation_results"] if row["scenario"] == EXPLORATORY else target["operation_results"]).append(out)
    # Firmware emits each cycle entry as scenario_terminal and its return as
    # the next transition_terminal. Pair in stream order; setup transitions
    # precede the first entry and therefore are not counted as cycle work.
    cycle_enters = [i for i, r in enumerate(rows) if r["event"] == "scenario_terminal" and r["scenario"] == "H-cycle"]
    cycle_returns = []
    for enter_position, index in enumerate(cycle_enters):
        next_enter = cycle_enters[enter_position + 1] if enter_position + 1 < len(cycle_enters) else len(rows)
        returned = next((r for r in rows[index + 1:next_enter] if r["event"] == "transition_terminal"), None)
        if returned is not None:
            cycle_returns.append(returned)
    scenarios["H-cycle"]["operation_results"].extend(
        {"operation_id":r["operation_id"],"terminal":r["terminal"],"terminal_count":r["terminal_count"],"reason":r["reason"],"widget_live":r["widget_live"],"timer_count":r["timer_count"]}
        for r in cycle_returns)
    results = scenarios
    checks = {}
    for s in DECIDED:
        vals = results[s]["operation_results"]
        if s == "H-cycle":
            ok = None if len(vals) != 240 else all(v["terminal"] == "success" and v["terminal_count"] == 1 and v["widget_live"] == 19 and v["timer_count"] == 1 for v in vals)
        elif s == "H-data":
            ok = None if len(vals) != 2 else all(v["terminal"] == "success" and v["terminal_count"] == 1 and v["update_level"] in (0,1) for v in vals)
        elif s == "H-data-during-exit":
            ok = None if len(vals) != 1 else vals[0]["terminal"] in ("success","stale") and vals[0]["old_panel_text_visible"] == 0
        elif s == "H-level2-control":
            ok = None if len(vals) != 1 else vals[0]["terminal"] == "failure" and vals[0]["reason"] == "level2_on_data"
        elif s == "H-destroyed":
            ok = None if len(vals) != 1 else vals[0]["terminal"] == "stale" and vals[0]["stale_handle_use"] == 0
        else:
            ok = None if len(vals) != 1 else vals[0]["terminal"] == "cancelled" and vals[0]["stage_before"] == vals[0]["stage_after"] and vals[0]["timer_count"] == 1
        checks[s] = ok
    experiment = _experiment_verdict(DECIDED, results, checks)
    summary_rows = [r for r in rows if r["event"] == "trace_summary"]
    if len(summary_rows) > 1:
        raise ParseError("duplicate trace summary")
    if summary_rows:
        summary = summary_rows[0]
        if summary["placeholder"] != 1:
            raise ParseError("exploratory placeholder missing")
    else:
        experiment = "INCOMPLETE" if experiment != "UNMEASURED" else experiment
    schema_file = Path(__file__).resolve().parents[1] / "trace-schema.json"
    try:
        schema_sha256 = hashlib.sha256(schema_file.read_bytes()).hexdigest()
    except OSError as exc:
        raise ParseError("LAB-06 trace schema identity is unavailable") from exc
    if not trusted_capture:
        # Host fixture parsing can exercise the L1 parser, but its contents do
        # not constitute a device experiment regardless of embedded markers.
        experiment = "UNMEASURED"
    metrics = {"document": DOCUMENT, "lab_id": "LAB-06", "lab_revision": "v2", "run_id": run_id,
        "parser": {"identity": PARSER_VERSION, "trace_schema": TRACE_SCHEMA},
        "source_identity": {"fixture_revision":"lab06-stage-shell/v1","trace_schema":TRACE_SCHEMA,"trace_schema_sha256":schema_sha256},
        "capture": {"mode":expected_mode if trusted_capture else None,"variant":expected_variant if trusted_capture else None,"post_reset":trusted_capture,"context":capture_context if trusted_capture else {},"build_id":expected_build_id if trusted_capture else None,"provenance":"device_adapter" if trusted_capture else "host_parse","evidence_level":"L2" if trusted_capture else "L1"},
        "evidence": {"trace_rows":len(rows),"operation_ids_unique":len(ids)==len(set(ids)),"summary_present":bool(summary_rows),"firmware_trace_loss":{"status":"UNKNOWN","count":None}},
        "lab_metrics": {"experiment_inputs":DECIDED,"scenarios":results,"experiment_verdict":experiment,
                        "performance_verdict":"UNMEASURED","series":{},
                        "queue_overflow_observations":[r for r in rows if r["event"]=="queue_overflow"],
                        "resource_samples":[r for r in rows if r["event"]=="resource_sample"],
                        "trace_summary":summary_rows[0] if summary_rows else None}}
    for field, unit in (("anim_us","us"),("build_us","us"),("lvgl_largest_free_bytes","bytes"),("lvgl_high_water_pct","percent")):
        vals = [r[field] for r in rows if r.get("event") in {"scenario_terminal","transition_terminal","resource_sample"} and field in r]
        metrics["lab_metrics"]["series"][field] = {"unit":unit,"sample_count":len(vals),"p50":_nearest(vals,50),"p95":_nearest(vals,95),"p99":_nearest(vals,99),"worst":max(vals) if vals else None,"missing_reason":None if vals else "no_device_samples"}
    return metrics


def parse_file(input_path: str | Path, output_path: str | Path | None = None, *, expected_run_id="fixture", **kwargs):
    source = Path(input_path)
    raw = source.read_bytes()
    if len(raw) > MAX_INPUT_BYTES:
        raise ParseError("serial input exceeds byte bound")
    metrics = parse_text(raw.decode("utf-8", errors="strict"), run_id=expected_run_id, **kwargs)
    metrics["source_identity"]["serial_sha256"] = hashlib.sha256(raw).hexdigest()
    if output_path is not None:
        target = Path(output_path)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(metrics, indent=2, sort_keys=True)+"\n", encoding="utf-8")
    return metrics


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("-o", "--output", required=True)
    parser.add_argument("--run-id", default="fixture")
    args = parser.parse_args()
    source = Path(args.input)
    try:
        lines = source.read_text(encoding="utf-8").splitlines()
        markers = [_marker(line, "LAB06_READY ") for line in lines if line.startswith("LAB06_READY ")]
        if len(markers) != 1:
            raise ParseError("missing or duplicate LAB06_READY marker")
        mode = markers[0].get("mode")
        parse_file(args.input, args.output, expected_run_id=args.run_id,
                   expected_mode=mode, expected_variant=mode, post_reset=False,
                   expected_build_id=markers[0].get("build"))
    except (OSError, UnicodeError, ParseError) as exc:
        raise SystemExit(f"LAB-06 parse failed: {exc}") from exc


if __name__ == "__main__":
    main()
