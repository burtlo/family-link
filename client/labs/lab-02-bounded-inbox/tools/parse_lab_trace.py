#!/usr/bin/env python3
"""Parse bounded LAB_TRACE rows into additive family-link.lab-metrics/v1 data.

The parser is deliberately evidence-first. It preserves the original
``event_counts`` and ``snap_events`` fields, but never turns an absent marker,
identity, interval, or loss counter into a zero or a successful claim.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from collections import Counter, defaultdict
from typing import Iterable, TextIO, Union

TRACE_MARKER = re.compile(r"\bLAB_TRACE\b")
READY_MARKER = re.compile(r"\bLAB02_READY\b(?P<fields>.*)")
HTTP_READY_MARKER = re.compile(r"\bLAB02_HTTP_READY\b(?P<fields>.*)")
LineSource = Union[Iterable[str], TextIO]

REQUIRED_FIELDS = ("t_us", "event", "gen", "focus", "msg")
OPTIONAL_INT_FIELDS = {
    "session_id", "op_id", "attempt", "snap_id", "gesture_id",
    "bind_generation", "card_slot", "page_index", "revision",
    "submitted_us", "started_us", "terminal_us", "elapsed_us",
    "queue_depth", "queue_high_water", "overflow", "focus_before",
    "focus_after", "server_completed_ns", "draw_buf_height_lines",
    "draw_buf_count", "draw_buf_bytes", "color_depth_bits",
    "internal_free_bytes", "internal_min_free_bytes", "internal_largest_free_bytes",
    "dma_free_bytes", "dma_largest_free_bytes", "psram_free_bytes",
    "psram_largest_free_bytes", "lvgl_mem_used_bytes", "lvgl_mem_high_water_bytes",
    "lvgl_biggest_free_bytes", "live_card_roots", "live_card_nodes", "active_fetches",
    "admitted_fetches", "fetch_queue_depth", "terminal_pending", "t_before_us",
    "t_after_us", "requested_bytes", "alignment_bytes", "capability_mask",
    "dma_largest_before_bytes", "dma_largest_after_bytes", "internal_largest_before_bytes",
    "internal_largest_after_bytes", "configured_bytes", "minimum_free_bytes",
    "sample_count", "max_live_card_nodes", "max_transient_bytes", "max_persistent_bytes",
    "max_concurrent_operations", "max_stack_bytes", "maximum_input_bytes",
    "fetch_queue_capacity", "terminal_capacity", "model_capacity", "page_capacity",
}
BOOL_FIELDS = {"publication_after_apply", "succeeded"}
STRING_FIELDS = {
    "trace_schema", "run_id", "op_kind", "stage_before", "stage_after",
    "state", "outcome", "reason", "status", "scenario", "build_id", "backend", "mode",
    "focus_msg_before", "focus_msg_after", "comparison_run_id",
    "phase", "owner", "method", "static_source", "resource_scope",
}
KNOWN_FIELDS = set(REQUIRED_FIELDS) | OPTIONAL_INT_FIELDS | BOOL_FIELDS | STRING_FIELDS
REQUIRED_COUNTS = (
    "bind", "unbind", "stale_bind_events", "wrong_content_events",
    "snap_started", "snap_success", "snap_stale", "snap_cancelled",
    "fetch_admitted", "fetch_terminal", "fetch_success", "fetch_failure",
    "fetch_timed_out", "fetch_stale", "rejected_delivery", "queue_full",
    "admission_busy", "trace_dropped", "malformed_trace", "duplicate_terminal",
    "orphan_terminal", "unterminated_operation",
)
DISTRIBUTION_NAMES = (
    "ui_tick_ms", "snap_ms", "bind_ms", "unbind_ms", "input_callback_ms",
    "animation_tick_ms", "presentation_update_ms", "page_apply_ms", "parse_ms",
    "fetch_queue_wait_ms", "fetch_attempt_ms", "input_to_visible_ms", "bind_wait_ms",
)
EVENT_DISTRIBUTIONS = {
    "input_callback_ms": {"input_callback"},
    "animation_tick_ms": {"animation_tick"},
    "presentation_update_ms": {"presentation_update"},
    "page_apply_ms": {"page_apply"},
    "parse_ms": {"parse", "page_parse"},
    "bind_ms": {"bind"},
    "unbind_ms": {"unbind"},
}


def _empty_distribution() -> dict:
    return {"unit": "ms", "n": 0, "p50": None, "p95": None, "p99": None, "worst": None}


def _distribution(samples: list[int]) -> dict:
    """Return exact nearest-rank percentiles for validated microseconds."""
    if not samples:
        return _empty_distribution()
    ordered = sorted(samples)

    def percentile(p: float) -> float:
        index = max(0, math.ceil(p * len(ordered)) - 1)
        return ordered[index] / 1000.0

    return {
        "unit": "ms", "n": len(ordered), "p50": percentile(0.50),
        "p95": percentile(0.95), "p99": percentile(0.99),
        "worst": ordered[-1] / 1000.0,
    }


def _token_fields(text: str) -> tuple[dict[str, str], list[str], set[str]]:
    """Parse key/value tokens while retaining harmless legacy bare extras."""
    fields: dict[str, str] = {}
    bare: list[str] = []
    duplicates: set[str] = set()
    for token in text.split():
        if "=" not in token:
            bare.append(token)
            continue
        key, value = token.split("=", 1)
        if not key or key in fields:
            duplicates.add(key)
            continue
        fields[key] = value
    return fields, bare, duplicates


def _parse_int(value: str | None, *, allow_dash: bool = True) -> int | None:
    if value is None or (allow_dash and value == "-"):
        return None
    try:
        return int(value, 10)
    except (TypeError, ValueError):
        return None


def _parse_ready_fields(text: str) -> dict[str, str]:
    fields, _, _ = _token_fields(text)
    return fields


def _parse_trace_line(line: str) -> tuple[dict | None, str | None]:
    marker = TRACE_MARKER.search(line)
    if not marker:
        return None, None
    fields, _bare, duplicates = _token_fields(line[marker.end():])
    malformed = bool(duplicates)
    if any(name not in fields for name in REQUIRED_FIELDS):
        malformed = True
    row: dict[str, object] = {name: fields.get(name) for name in REQUIRED_FIELDS}
    row["msg"] = None if fields.get("msg") in (None, "-") else fields.get("msg")
    for name in OPTIONAL_INT_FIELDS:
        value = _parse_int(fields.get(name))
        if name in fields and value is None and fields[name] != "-":
            malformed = True
        row[name] = value
    for name in BOOL_FIELDS:
        if fields.get(name) in {"true", "1"}:
            row[name] = True
        elif fields.get(name) in {"false", "0"}:
            row[name] = False
        elif name in fields and fields[name] != "-":
            malformed = True
        else:
            row[name] = None
    for name in STRING_FIELDS:
        row[name] = None if fields.get(name) in (None, "-") else fields[name]
    t_us = _parse_int(fields.get("t_us"), allow_dash=False)
    generation = _parse_int(fields.get("gen"), allow_dash=False)
    focus = _parse_int(fields.get("focus"), allow_dash=False)
    if t_us is None or generation is None or focus is None or fields.get("event") in (None, "", "-"):
        malformed = True
    if malformed:
        return None, "malformed_trace"
    row["t_us"] = t_us
    row["gen"] = generation
    row["focus"] = focus
    row["event"] = fields["event"]
    row["extras"] = {key: value for key, value in fields.items() if key not in KNOWN_FIELDS}
    row["bare_extras"] = list(_bare)
    row["_fields"] = fields
    return row, None


def _metadata(lines: list[str], rows: list[dict], invalid: list[str]) -> tuple[str | None, dict]:
    """Collect only runtime markers and trace context that are actually seen."""
    ready_values: dict[str, set[str]] = defaultdict(set)
    for line in lines:
        match = READY_MARKER.search(line)
        if match:
            fields = _parse_ready_fields(match.group("fields"))
            for key in ("build", "build_id", "fixture", "mode", "backend", "scenario",
                        "trace_schema", "run_id", "comparison_run_id",
                        "draw_buf_height_lines", "draw_buf_count", "draw_buf_bytes",
                        "color_depth_bits"):
                if key in fields:
                    ready_values[key].add(fields[key])
        http = HTTP_READY_MARKER.search(line)
        if http:
            fields = _parse_ready_fields(http.group("fields"))
            if "scenario" in fields:
                ready_values["scenario"].add(fields["scenario"])

    def one(name: str) -> str | None:
        values = set(ready_values.get(name, set()))
        values.update(str(row[name]) for row in rows if row.get(name) not in (None, ""))
        if len(values) > 1:
            invalid.append(f"metadata.{name}_mismatch")
            return None
        return next(iter(values), None)

    run_ids = set(ready_values.get("run_id", set()))
    run_ids.update(str(row["run_id"]) for row in rows if row.get("run_id") not in (None, ""))
    if len(run_ids) > 1:
        invalid.append("run_id_mismatch")
    run_id = next(iter(run_ids), None)
    trace_schemas = {str(row["trace_schema"]) for row in rows if row.get("trace_schema") not in (None, "")}
    if len(trace_schemas) > 1:
        invalid.append("trace_schema_mismatch")
    trace_schema = next(iter(trace_schemas), None) or one("trace_schema")

    fixture = one("fixture")
    fixture_size = None
    if fixture and fixture.startswith("inbox_"):
        fixture_size = _parse_int(fixture.removeprefix("inbox_"), allow_dash=False)
        if fixture_size is None:
            invalid.append("metadata.fixture_size_invalid")
    mode = one("mode")
    if mode == "http":
        mode = "recycle"
    if mode not in (None, "recycle", "eager"):
        invalid.append("metadata.mode_invalid")
        mode = None

    backend_values = set(ready_values.get("backend", set()))
    backend_values.update(str(row["backend"]) for row in rows if row.get("backend"))
    if any(str(row.get("op_kind", "")).startswith("http") for row in rows):
        backend_values.add("http")
    if any(row.get("event") in {"page_fetch", "fetch_terminal", "fetch_admitted"} for row in rows):
        backend_values.add("http")
    if len(backend_values) > 1:
        invalid.append("metadata.backend_mismatch")
    backend = next(iter(backend_values), None)
    if backend not in (None, "local", "http"):
        invalid.append("metadata.backend_invalid")
        backend = None
    effective_config: dict[str, int | None] = {}
    for name in ("draw_buf_height_lines", "draw_buf_count", "draw_buf_bytes", "color_depth_bits"):
        # Ordinary trace/resource rows are not configuration authority. Only
        # the validated startup marker (or a future manifest adapter) may set
        # these values.
        values = {_parse_int(value, allow_dash=False)
                  for value in ready_values.get(name, set())}
        values.discard(None)
        if len(values) > 1:
            invalid.append(f"metadata.effective_config.{name}_mismatch")
            effective_config[name] = None
        else:
            effective_config[name] = next(iter(values), None)
    build_values = set(ready_values.get("build", set()))
    build_values.update(ready_values.get("build_id", set()))
    build_values.update(str(row["build_id"]) for row in rows if row.get("build_id") not in (None, ""))
    if len(build_values) > 1:
        invalid.append("metadata.build_id_mismatch")
    build_id = next(iter(build_values), None)
    return run_id, {
        "fixture_size": fixture_size,
        "mode": mode,
        "backend": backend,
        "scenario": one("scenario"),
        "build_id": build_id,
        "trace_schema": trace_schema,
        "comparison_run_id": one("comparison_run_id"),
        "effective_config": effective_config,
    }


def _identity(row: dict, kind: str) -> tuple | None:
    if kind == "fetch":
        names = ("run_id", "session_id", "op_kind", "op_id", "attempt")
    elif kind == "snap":
        names = ("run_id", "session_id", "snap_id")
    else:
        return None
    values = tuple(row.get(name) for name in names)
    return None if any(value in (None, "", "-") for value in values) else values


def _missing_identity_fields(row: dict, kind: str) -> tuple[str, ...]:
    if kind == "fetch":
        names = ("run_id", "session_id", "op_kind", "op_id", "attempt")
    elif kind == "snap":
        names = ("run_id", "session_id", "snap_id")
    else:
        return ()
    return tuple(name for name in names if row.get(name) in (None, "", "-"))


def _bind_identity(row: dict) -> tuple | None:
    names = ("run_id", "session_id", "card_slot", "bind_generation", "revision")
    values = tuple(row.get(name) for name in names)
    return None if any(value in (None, "", "-") for value in values) else values


def _visible_identity(row: dict) -> tuple | None:
    values = (row.get("run_id"), row.get("session_id"), row.get("card_slot"),
              row.get("revision"), row.get("msg"))
    if any(value in (None, "", "-") for value in values):
        return None
    if row.get("publication_after_apply") is not True:
        return None
    return values


def _gesture_identity(row: dict) -> tuple | None:
    values = (row.get("run_id"), row.get("session_id"), row.get("gesture_id"))
    return None if any(value in (None, "", "-") for value in values) else values


def _apply_identity(row: dict) -> tuple | None:
    values = (row.get("run_id"), row.get("session_id"), row.get("revision"),
              row.get("op_id"))
    return None if any(value in (None, "", "-") for value in values) else values


def _valid_elapsed(row: dict, invalid: list[str], label: str) -> int | None:
    elapsed = row.get("elapsed_us")
    submitted = row.get("submitted_us")
    if elapsed is not None:
        if not isinstance(elapsed, int) or elapsed < 0:
            invalid.append(f"{label}.negative_or_nonmonotonic_elapsed")
            return None
        started = row.get("started_us")
        terminal = row.get("terminal_us")
        if isinstance(started, int) and isinstance(terminal, int) and terminal < started:
            invalid.append(f"{label}.negative_or_nonmonotonic_elapsed")
            return None
        if isinstance(submitted, int) and isinstance(started, int) and started < submitted:
            invalid.append(f"{label}.negative_or_nonmonotonic_elapsed")
            return None
        return elapsed
    started = row.get("started_us")
    terminal = row.get("terminal_us")
    if started is not None or terminal is not None:
        if not isinstance(started, int) or not isinstance(terminal, int) or terminal < started:
            invalid.append(f"{label}.negative_or_nonmonotonic_elapsed")
            return None
        if isinstance(submitted, int) and started < submitted:
            invalid.append(f"{label}.negative_or_nonmonotonic_elapsed")
            return None
        return terminal - started
    return None


def _loss_from_lines(lines: list[str], rows: list[dict], malformed_rows: int, invalid: list[str]) -> dict:
    observed = False
    dropped: int | None = None
    oversized: int | None = None
    malformed: int | None = None
    declared_status: str | None = None

    def note_status(status: str | None) -> None:
        nonlocal declared_status
        if status not in {"NONE", "OBSERVED", "UNKNOWN"}:
            return
        if status == "OBSERVED" or declared_status == "OBSERVED":
            declared_status = "OBSERVED"
        elif status == "UNKNOWN" or declared_status == "UNKNOWN":
            declared_status = "UNKNOWN"
        else:
            declared_status = "NONE"

    def note_count(current: int | None, value: int | None) -> int | None:
        if value is None:
            return current
        return value if current is None else max(current, value)

    for line in lines:
        if "LAB_TRACE_LOSS" not in line and "LAB_TRACE_STATS" not in line:
            continue
        suffix = line.split("LAB_TRACE_LOSS", 1)[-1].split("LAB_TRACE_STATS", 1)[-1]
        fields, _, duplicates = _token_fields(suffix)
        if duplicates:
            invalid.append("trace_loss.duplicate_field")
        observed = True
        status = fields.get("status")
        note_status(status)
        dropped = note_count(dropped, _parse_int(fields.get("dropped_events", fields.get("dropped"))))
        oversized = note_count(oversized, _parse_int(fields.get("oversized_events", fields.get("oversized"))))
        malformed = note_count(malformed, _parse_int(fields.get("malformed_rows", fields.get("malformed"))))
    for row in rows:
        if row.get("event") == "trace_loss":
            observed = True
            fields = row.get("_fields", {})
            status = str(row.get("status") or row.get("state") or row.get("outcome") or "").upper()
            note_status(status)
            dropped = note_count(dropped, _parse_int(fields.get("dropped_events", fields.get("dropped"))))
            oversized = note_count(oversized, _parse_int(fields.get("oversized_events", fields.get("oversized"))))
            malformed = note_count(malformed, _parse_int(fields.get("malformed_rows", fields.get("malformed"))))
        if row.get("overflow") is not None:
            observed = True
            overflow = row.get("overflow")
            if isinstance(overflow, int):
                dropped = (dropped or 0) + overflow
    if malformed_rows:
        observed = True
        malformed = (malformed or 0) + malformed_rows
    if not observed:
        return {"status": "UNKNOWN", "dropped_events": None, "oversized_events": None,
                "malformed_rows": None}
    if any(value is None for value in (dropped, oversized, malformed)):
        return {"status": "OBSERVED", "dropped_events": dropped, "oversized_events": oversized,
                "malformed_rows": malformed}
    if declared_status in {"OBSERVED", "UNKNOWN"}:
        if declared_status == "UNKNOWN" and not (dropped or oversized or malformed):
            return {"status": "UNKNOWN", "dropped_events": None, "oversized_events": None,
                    "malformed_rows": None}
        return {"status": "OBSERVED", "dropped_events": dropped, "oversized_events": oversized,
                "malformed_rows": malformed}
    status = "OBSERVED" if dropped or oversized or malformed else "NONE"
    return {"status": status, "dropped_events": dropped, "oversized_events": oversized,
            "malformed_rows": malformed}


def _validate_operations(rows: list[dict], invalid: list[str]) -> tuple[dict[str, int], set[tuple]]:
    starts: dict[tuple, list[dict]] = defaultdict(list)
    terminals: dict[tuple, list[dict]] = defaultdict(list)
    resets: list[dict] = []
    invalid_terminal_keys: set[tuple] = set()
    previous_t_us: int | None = None
    for row in rows:
        t_us = row.get("t_us")
        if isinstance(t_us, int):
            if previous_t_us is not None and t_us < previous_t_us:
                invalid.append("trace_timestamp_nonmonotonic")
            previous_t_us = t_us
        event = row["event"]
        if event == "session_reset":
            resets.append(row)
            continue
        if event in {"fetch_admitted", "fetch_terminal"}:
            key = _identity(row, "fetch")
            if key is None:
                missing = _missing_identity_fields(row, "fetch")
                invalid.append(f"{event}_missing_identity")
                invalid.extend(f"{event}_missing_{name}" for name in missing)
            elif event == "fetch_admitted":
                starts[("fetch", key)].append(row)
            else:
                terminals[("fetch", key)].append(row)
        elif event in {"snap_start", "snap_end"}:
            key = _identity(row, "snap")
            if key is None:
                missing = _missing_identity_fields(row, "snap")
                invalid.append(f"{event}_missing_identity")
                invalid.extend(f"{event}_missing_{name}" for name in missing)
            elif event == "snap_start":
                starts[("snap", key)].append(row)
            else:
                terminals[("snap", key)].append(row)
        elif event in {"io_start_admitted", "io_start_terminal"}:
            key = (row.get("run_id"), row.get("session_id"), row.get("op_kind"))
            if None in key or "" in key:
                invalid.append(f"{event}_missing_identity")
            elif event == "io_start_admitted":
                starts[("io", key)].append(row)
            else:
                terminals[("io", key)].append(row)
    duplicate = orphan = unterminated = 0
    for key, values in terminals.items():
        if len(values) > 1:
            duplicate += len(values) - 1
            invalid.append(f"duplicate_terminal:{key[0]}")
            invalid_terminal_keys.add(key)
        if key not in starts:
            orphan += len(values)
            invalid.append(f"orphan_terminal:{key[0]}")
            invalid_terminal_keys.add(key)
        for terminal in values:
            outcome = terminal.get("outcome")
            if key[0] in {"fetch", "snap"} and outcome not in {
                    "success", "failure", "cancelled", "stale", "timed_out"}:
                invalid.append(f"{key[0]}_terminal_invalid_outcome")
                invalid_terminal_keys.add(key)
    for key, values in starts.items():
        if key not in terminals:
            unterminated += len(values)
            invalid.append(f"unterminated_operation:{key[0]}")
            invalid_terminal_keys.add(key)
        elif len(values) > 1:
            invalid.append(f"duplicate_start:{key[0]}")
            invalid_terminal_keys.add(key)
        for start in values:
            for terminal in terminals.get(key, []):
                if terminal.get("t_us", -1) < start.get("t_us", -1):
                    invalid.append(f"{key[0]}_terminal_before_start")
                    invalid_terminal_keys.add(key)
    for reset in resets:
        reset_time = reset.get("t_us")
        for key, admitted in starts.items():
            for start in admitted:
                if not isinstance(reset_time, int) or reset_time <= start.get("t_us", -1):
                    continue
                matching_terminals = terminals.get(key, [])
                if not matching_terminals or any(term.get("t_us", -1) > reset_time for term in matching_terminals):
                    invalid.append("session_reset_violation")
                    invalid_terminal_keys.add(key)
    return ({"duplicate_terminal": duplicate, "orphan_terminal": orphan,
             "unterminated_operation": unterminated}, invalid_terminal_keys)


def _add_count(counts: dict[str, int | None], name: str, value: int = 1) -> None:
    counts[name] = (counts[name] or 0) + value


RESOURCE_SAMPLE_FIELDS = (
    "phase", "t_us", "session_id", "op_id", "internal_free_bytes",
    "internal_min_free_bytes", "internal_largest_free_bytes", "dma_free_bytes",
    "dma_largest_free_bytes", "psram_free_bytes", "psram_largest_free_bytes",
    "lvgl_mem_used_bytes", "lvgl_mem_high_water_bytes", "lvgl_biggest_free_bytes",
    "live_card_roots", "live_card_nodes", "active_fetches", "admitted_fetches",
    "fetch_queue_depth", "terminal_pending",
)
RESOURCE_REDUCTIONS = (
    "lvgl_mem_used_bytes", "lvgl_mem_high_water_bytes", "live_card_nodes",
    "fetch_queue_depth", "terminal_pending",
)
ENVELOPE_FIELDS = (
    "max_live_card_nodes", "max_transient_bytes", "max_persistent_bytes",
    "max_concurrent_operations", "max_stack_bytes", "maximum_input_bytes",
    "fetch_queue_capacity", "terminal_capacity", "model_capacity", "page_capacity",
)


def _observed_resources(rows: list[dict], invalid: list[str]) -> dict:
    """Reduce only typed resource markers; never infer peaks from heap deltas."""
    samples: list[dict] = []
    draw_allocations: list[dict] = []
    stacks: dict[str, dict] = {}
    declared = {name: None for name in ENVELOPE_FIELDS}
    measured = {name: None for name in ENVELOPE_FIELDS}

    for row in rows:
        event = str(row.get("event"))
        if event in {"resource_sample", "resource"}:
            sample = {name: row.get(name) for name in RESOURCE_SAMPLE_FIELDS}
            sample["phase"] = row.get("phase")
            if sample["phase"] is None:
                invalid.append("resource_sample_missing_phase")
            samples.append(sample)
        elif event in {"draw_allocation", "draw_alloc"}:
            allocation = {name: row.get(name) for name in (
                "t_before_us", "t_after_us", "requested_bytes", "alignment_bytes",
                "capability_mask", "succeeded", "dma_largest_before_bytes",
                "dma_largest_after_bytes", "internal_largest_before_bytes",
                "internal_largest_after_bytes")}
            draw_allocations.append(allocation)
        elif event in {"stack_sample", "stack"}:
            owner = row.get("owner")
            if not owner:
                invalid.append("stack_sample_missing_owner")
                continue
            value = {
                "configured_bytes": row.get("configured_bytes"),
                "minimum_free_bytes": row.get("minimum_free_bytes"),
                "method": row.get("method"),
                "sample_count": row.get("sample_count"),
            }
            prior = stacks.get(str(owner))
            if prior is not None and prior != value:
                invalid.append(f"resource_stack_mismatch:{owner}")
            stacks[str(owner)] = value
        elif event in {"resource_envelope", "envelope"}:
            target = str(row.get("state") or row.get("resource_scope") or "").lower()
            destination = declared if target == "declared" else measured if target in {"measured", "measured_maxima"} else None
            if destination is None:
                invalid.append("resource_envelope_missing_scope")
                continue
            for name in ENVELOPE_FIELDS:
                if row.get(name) is not None:
                    if destination[name] is not None and destination[name] != row[name]:
                        invalid.append(f"resource_envelope_mismatch:{target}.{name}")
                    destination[name] = row[name]

    maxima: dict[str, int | None] = {}
    minima: dict[str, int | None] = {}
    for name in RESOURCE_REDUCTIONS:
        values = [sample[name] for sample in samples if isinstance(sample.get(name), int)]
        if not values:
            maxima[name] = None
            minima[name] = None
        else:
            maxima[name] = max(values)
            minima[name] = min(values)

    peaks = {
        "lvgl_mem_used": maxima.get("lvgl_mem_used_bytes"),
        "lvgl_mem_high_water": maxima.get("lvgl_mem_high_water_bytes"),
        "lvgl_biggest_free": None,
        "internal_largest_free": None,
        "psram_largest_free": None,
        "live_widget_count": maxima.get("live_card_nodes"),
    }
    associated_provenance = {
        "lvgl_biggest_free": None,
        "internal_largest_free": None,
        "psram_largest_free": None,
    }
    used_samples = [sample for sample in samples
                    if isinstance(sample.get("lvgl_mem_used_bytes"), int)]
    max_used = max((sample["lvgl_mem_used_bytes"] for sample in used_samples), default=None)
    peak_samples = [sample for sample in used_samples
                    if sample["lvgl_mem_used_bytes"] == max_used]
    if len(peak_samples) == 1:
        selected = peak_samples[0]
        peaks["lvgl_biggest_free"] = selected.get("lvgl_biggest_free_bytes")
        peaks["internal_largest_free"] = selected.get("internal_largest_free_bytes")
        peaks["psram_largest_free"] = selected.get("psram_largest_free_bytes")
        provenance = {
            "sample_index": samples.index(selected),
            "t_us": selected.get("t_us"),
            "phase": selected.get("phase"),
        }
        for name in associated_provenance:
            associated_provenance[name] = provenance

    return {
        "samples": samples,
        "draw_allocations": draw_allocations,
        "stacks": stacks,
        "static_bytes": {
            "model": None, "metadata": None, "terminal_slots": None,
            "pending_revision": None, "http_scratch": None, "http_result": None,
            "card_state": None, "trace_storage": None, "dram_data": None,
            "dram_bss": None, "static_source": None,
        },
        "envelopes": {"declared": declared, "measured_maxima": measured},
        "minima": minima,
        "maxima": maxima,
        "peaks": peaks,
        "associated_provenance": associated_provenance,
    }


def parse_trace(lines: LineSource) -> dict:
    """Parse serial lines into additive ``family-link.lab-metrics/v1`` data."""
    materialized = list(lines)
    events: Counter[str] = Counter()
    rows: list[dict] = []
    malformed_rows = 0
    harness_printf_success = 0
    harness_printf_error = 0
    for line in materialized:
        row, error = _parse_trace_line(line)
        if error:
            malformed_rows += 1
            continue
        if row is not None:
            rows.append(row)
            events[str(row["event"])] += 1
            continue
        if line.startswith("LAB02_READY"):
            events["boot_ready"] += 1
        elif "HARNESS_DONE" in line:
            if "status=" in line:
                harness_printf_error += 1
            else:
                harness_printf_success += 1
        elif "LAB02_HTTP_READY" in line:
            events["http_ready"] += 1
    if events.get("harness_done", 0) == 0 and harness_printf_success > 0:
        events["harness_done"] = harness_printf_success
    if harness_printf_error:
        events["harness_abort"] = harness_printf_error

    invalid: list[str] = []
    run_id, metadata = _metadata(materialized, rows, invalid)
    trace_loss = _loss_from_lines(materialized, rows, malformed_rows, invalid)
    operation_counts, invalid_terminal_keys = _validate_operations(rows, invalid)
    counts: dict[str, int | None] = {name: None for name in REQUIRED_COUNTS}
    direct_events = {
        "bind": "bind", "unbind": "unbind", "bind_stale": "stale_bind_events",
        "wrong_content": "wrong_content_events", "snap_start": "snap_started",
        "fetch_admitted": "fetch_admitted", "fetch_terminal": "fetch_terminal",
        "fetch_delivery_rejected": "rejected_delivery", "fetch_queue_full": "queue_full",
        "fetch_admission_busy": "admission_busy",
    }
    for row in rows:
        event = str(row["event"])
        if direct_events.get(event):
            _add_count(counts, direct_events[event])
        outcome = str(row.get("outcome") or "")
        if event == "snap_end":
            outcome_map = {"success": "snap_success", "stale": "snap_stale", "cancelled": "snap_cancelled"}
            if outcome_map.get(outcome):
                _add_count(counts, outcome_map[outcome])
        if event == "fetch_terminal":
            outcome_map = {"success": "fetch_success", "failure": "fetch_failure",
                           "timed_out": "fetch_timed_out", "stale": "fetch_stale"}
            if outcome_map.get(outcome):
                _add_count(counts, outcome_map[outcome])
    for name, value in operation_counts.items():
        if value:
            counts[name] = value
    counts["malformed_trace"] = malformed_rows or None
    if trace_loss["dropped_events"] is not None:
        counts["trace_dropped"] = trace_loss["dropped_events"]

    invalid_timing: list[str] = []
    samples: dict[str, list[int]] = defaultdict(list)
    applicable: set[str] = set()
    outcome_samples: dict[str, dict[str, list[int]]] = defaultdict(lambda: defaultdict(list))
    fetch_rows: dict[tuple, list[dict]] = defaultdict(list)
    fetch_started_rows: dict[tuple, list[dict]] = defaultdict(list)
    tick_times: dict[object, int] = {}
    apply_keys = {_apply_identity(row) for row in rows
                  if row.get("event") in {"page_apply", "apply"}}
    apply_keys.discard(None)
    gesture_keys = {_gesture_identity(row) for row in rows
                    if row.get("event") in {"gesture", "gesture_start", "gesture_end", "input"}}
    gesture_keys.discard(None)
    visible_keys = {_visible_identity(row) for row in rows
                    if row.get("event") in {"bind", "publication", "visible_content"}}
    visible_keys.discard(None)
    for row in rows:
        event = str(row["event"])
        if event in {"ui_tick", "owner_wake", "ui_wake"}:
            applicable.add("ui_tick_ms")
            tick_key = row.get("session_id", "unknown")
            previous = tick_times.get(tick_key)
            if isinstance(previous, int) and row["t_us"] >= previous:
                samples["ui_tick_ms"].append(row["t_us"] - previous)
            elif previous is not None:
                invalid_timing.append("ui_tick_ms.negative_or_nonmonotonic_elapsed")
            tick_times[tick_key] = row["t_us"]
        for distribution_name, event_names in EVENT_DISTRIBUTIONS.items():
            if event in event_names:
                applicable.add(distribution_name)
                elapsed = _valid_elapsed(row, invalid_timing, distribution_name)
                identity_valid = True
                if distribution_name == "bind_ms" and _bind_identity(row) is None:
                    invalid_timing.append("bind_ms_missing_identity")
                    identity_valid = False
                if distribution_name == "unbind_ms" and _bind_identity(row) is None:
                    invalid_timing.append("unbind_ms_missing_identity")
                    identity_valid = False
                if distribution_name == "page_apply_ms" and _apply_identity(row) is None:
                    invalid_timing.append("page_apply_ms_missing_identity")
                    identity_valid = False
                if elapsed is None or not identity_valid:
                    continue
                samples[distribution_name].append(elapsed)
        if event == "snap_end":
            applicable.add("snap_ms")
            elapsed = _valid_elapsed(row, invalid_timing, "snap_ms")
            outcome = str(row.get("outcome") or "unknown")
            snap_key = _identity(row, "snap")
            if (elapsed is not None and snap_key is not None
                    and ("snap", snap_key) not in invalid_terminal_keys):
                outcome_samples["snap_ms"][outcome].append(elapsed)
                if outcome == "success":
                    samples["snap_ms"].append(elapsed)
        if event == "fetch_terminal":
            applicable.add("fetch_attempt_ms")
            outcome = str(row.get("outcome") or "unknown")
            fetch_key = _identity(row, "fetch")
            started_us = row.get("started_us") if isinstance(row.get("started_us"), int) else None
            if fetch_key is not None and started_us is None:
                starts = fetch_started_rows.get(fetch_key, [])
                if len(starts) == 1:
                    started_us = starts[0].get("started_us")
            if not isinstance(started_us, int):
                invalid_timing.append("fetch_attempt_ms_missing_worker_start")
                elapsed = None
            elif not isinstance(row.get("terminal_us"), int):
                invalid_timing.append("fetch_attempt_ms_missing_terminal_timestamp")
                elapsed = None
            else:
                elapsed = _valid_elapsed(
                    {**row, "elapsed_us": row["terminal_us"] - started_us,
                     "started_us": started_us},
                    invalid_timing, "fetch_attempt_ms")
            if (elapsed is not None and fetch_key is not None
                    and ("fetch", fetch_key) not in invalid_terminal_keys):
                outcome_samples["fetch_attempt_ms"][outcome].append(elapsed)
                if outcome == "success":
                    samples["fetch_attempt_ms"].append(elapsed)
        if event == "fetch_started":
            fetch_key = _identity(row, "fetch")
            if fetch_key is None:
                invalid_timing.append("fetch_started_missing_identity")
            elif isinstance(row.get("started_us"), int):
                fetch_started_rows[fetch_key].append(row)
        if event in {"fetch_admitted", "fetch_started", "fetch_terminal"}:
            fetch_key = _identity(row, "fetch")
            if fetch_key is not None and ("fetch", fetch_key) not in invalid_terminal_keys:
                fetch_rows[fetch_key].append(row)
            submitted, started = row.get("submitted_us"), row.get("started_us")
        if event == "input_to_visible":
            applicable.add("input_to_visible_ms")
            visible_key = _visible_identity(row)
            gesture_key = _gesture_identity(row)
            apply_key = _apply_identity(row)
            if (visible_key is None or gesture_key is None or apply_key is None
                    or visible_key not in visible_keys
                    or gesture_key not in gesture_keys
                    or apply_key not in apply_keys):
                invalid_timing.append("input_to_visible_missing_correlation")
                continue
            elapsed = _valid_elapsed(row, invalid_timing, "input_to_visible_ms")
            if elapsed is not None:
                samples["input_to_visible_ms"].append(elapsed)
        if event == "bind" and row.get("submitted_us") is not None:
            applicable.add("bind_wait_ms")
            if _bind_identity(row) is None:
                invalid_timing.append("bind_wait_ms_missing_identity")
                continue
            submitted = row.get("submitted_us")
            if isinstance(submitted, int) and row["t_us"] >= submitted:
                samples["bind_wait_ms"].append(row["t_us"] - submitted)
            else:
                invalid_timing.append("bind_wait_ms.negative_or_nonmonotonic_elapsed")
    for fetch_key, fetch_events in fetch_rows.items():
        submitted = next((row.get("submitted_us") for row in fetch_events
                           if isinstance(row.get("submitted_us"), int)), None)
        started = next((row.get("started_us") for row in fetch_events
                         if isinstance(row.get("started_us"), int)), None)
        if submitted is not None and started is not None:
            applicable.add("fetch_queue_wait_ms")
            if started >= submitted:
                samples["fetch_queue_wait_ms"].append(started - submitted)
            else:
                invalid_timing.append("fetch_queue_wait_ms.negative_or_nonmonotonic_elapsed")
    invalid.extend(invalid_timing)
    distributions = {
        name: _distribution(samples[name]) if name in applicable else None
        for name in DISTRIBUTION_NAMES
    }
    outcome_distributions = {
        name: {outcome: _distribution(values) for outcome, values in by_outcome.items()}
        for name, by_outcome in outcome_samples.items()
    }
    resources = _observed_resources(rows, invalid)

    missing: list[str] = []
    if run_id is None:
        missing.append("run_id")
    if not any(row.get("session_id") is not None for row in rows):
        missing.append("session_id")
    if trace_loss["status"] == "UNKNOWN":
        missing.append("trace_loss")
    missing.append("capture_interval")
    for name in DISTRIBUTION_NAMES:
        if distributions[name] is None or distributions[name]["n"] == 0:
            missing.append(f"timing.{name}")
    if not resources["samples"]:
        missing.append("resource_samples")
    if not any(value is not None for value in metadata["effective_config"].values()):
        missing.append("effective_config")
    missing.append("visual_confirmation")
    missing = sorted(set(missing))
    invalid = sorted(set(invalid))
    evidence_status = "INCOMPLETE" if invalid or trace_loss["status"] != "NONE" or missing else "COMPLETE"
    if invalid_timing or any("terminal" in item or "operation" in item for item in invalid):
        timing_status = "INCOMPLETE"
    elif any(value and value["n"] for value in distributions.values() if value is not None):
        timing_status = "MEASURED"
    else:
        timing_status = "UNMEASURED"

    return {
        "document": "family-link.lab-metrics/v1",
        "event_counts": dict(events),
        "snap_events": sum(1 for row in rows if row["event"] in ("snap_start", "snap_end")),
        "run_id": run_id,
        "metadata": metadata,
        "distributions": distributions,
        "distribution_outcomes": outcome_distributions,
        "peaks": resources["peaks"],
        "resource_samples": resources["samples"],
        "resources": {
            "samples": resources["samples"],
            "draw_allocations": resources["draw_allocations"],
            "stacks": resources["stacks"],
            "static_bytes": resources["static_bytes"],
            "envelopes": resources["envelopes"],
            "minima": resources["minima"],
            "maxima": resources["maxima"],
            "associated_provenance": resources["associated_provenance"],
        },
        "counts": counts,
        "trace_loss": trace_loss,
        "evidence": {
            "status": evidence_status,
            "missing": missing,
            "invalid": invalid,
            "timing_status": timing_status,
            "visual_status": "UNMEASURED",
            "budget_status": "OPEN",
        },
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("trace", nargs="?", type=argparse.FileType("r"), default=sys.stdin)
    ap.add_argument("-o", "--output", help="Write metrics JSON path")
    args = ap.parse_args()
    metrics = parse_trace(args.trace)
    out = json.dumps(metrics, indent=2)
    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(out)
    else:
        print(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
