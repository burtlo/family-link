#!/usr/bin/env python3
"""Bounded LAB-10 observation parser and metric aggregator.

The capture, renderer, audio model, and workload components intentionally keep
their own ownership boundaries.  This module is the small host-side boundary
that joins their structured observations into the package metric contract.
It is deliberately dependency-free so it can run in the lab host and in a
native/unit-test environment.

Input document (``lab10-metrics-observation/v1``)::

    {
      "observation_schema": "lab10-metrics-observation/v1",
      "provenance": "host",
      "workload": { ... lab10-workload/v1 metadata ... },
      "trace": { ... optional lab10-record-trace/v1 ... },
      "fields": {
        "raw_report_count": {"value": 12, "unit": "count"},
        "touch_to_ink_us": {"samples": [11, 13, 17], "unit": "us"}
      },
      "repeated_runs": [
        {"run_id": "r1", "memory_before_bytes": 1000,
         "memory_after_bytes": 1004}
      ]
    }

The output has one typed record for every field in manifest order.  Percentile
series are keyed by their base observation name (for example,
``touch_to_ink_us``) and produce the declared ``*_p50``, ``*_p95``, ``*_p99``,
and ``*_max`` fields.  The aggregator never accepts a precomputed percentile:
the four values are emitted only when the base series contains real samples.

Raw/display/logical coordinates, color changes, stroke boundaries, pen-up
boundaries, and workload metadata remain in ``preserved``.  They are evidence
records, not replacement metric fields.  The parser fails closed for unknown
keys, malformed values, conflicting provenance, non-monotonic traces, and
inputs beyond the fixed limits below.
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


PACKAGE_DIR = Path(__file__).resolve().parents[1]
MANIFEST_PATH = PACKAGE_DIR / "manifest.lab.json"
TRACE_SCHEMA_PATH = PACKAGE_DIR / "trace-schema.v1.json"

OBSERVATION_SCHEMA = "lab10-metrics-observation/v1"
PROVENANCE_VALUES = ("hardware", "host", "derived_static", "unmeasured/blocked")
PERCENTILES = ("p50", "p95", "p99", "max")

# These are parser limits, not firmware limits.  They prevent a malformed host
# artifact from becoming an unbounded allocation or output document.
MAX_DOCUMENT_BYTES = 2 * 1024 * 1024
MAX_OUTPUT_BYTES = 4 * 1024 * 1024
MAX_TRACE_EVENTS = 16_384
MAX_SAMPLES_PER_SERIES = 4_096
MAX_TOTAL_SAMPLES = 32_768
MAX_REPEATED_RUNS = 256
MAX_PRESERVED_RECORDS = 16_384

UINT8_MAX = 2**8 - 1
UINT32_MAX = 2**32 - 1
UINT64_MAX = 2**64 - 1
VALID_SESSION_GENERATION_MAX = UINT64_MAX - 1

MISSING_NO_SAMPLES = "no real samples supplied for this metric"
MISSING_NO_OBSERVATION = "no observation supplied"
MISSING_HARDWARE_TASK = (
    "hardware-only metric; host cannot measure device task scheduling or stack high-water"
)
MISSING_HARDWARE_MEMORY = (
    "hardware-only metric; host cannot observe device heap/PSRAM free space or fragmentation"
)


class MetricsError(ValueError):
    """Raised when an observation artifact is malformed or exceeds a bound."""


@dataclass(frozen=True)
class MetricRecord:
    """One field-level metric record in the exact schema order."""

    value: Any
    unit: str
    provenance: str
    sample_count: int
    missing_reason: str | None

    def to_dict(self) -> dict[str, Any]:
        # Keep this order aligned with metric_schema.field_record.
        return {
            "value": self.value,
            "unit": self.unit,
            "provenance": self.provenance,
            "sample_count": self.sample_count,
            "missing_reason": self.missing_reason,
        }


@dataclass(frozen=True)
class AggregationResult:
    """Typed metrics plus the evidence that must not be collapsed into counts."""

    metrics: dict[str, MetricRecord]
    preserved: dict[str, Any]

    def to_dict(self) -> dict[str, Any]:
        return {
            "metric_schema": "manifest.lab.json#/metric_schema",
            "metrics": {name: record.to_dict() for name, record in self.metrics.items()},
            "preserved": copy.deepcopy(self.preserved),
        }


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise MetricsError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json_bytes(payload: bytes) -> Any:
    """Decode bounded JSON with duplicate-key and non-finite-number rejection."""

    if not isinstance(payload, bytes):
        raise MetricsError("JSON payload must be bytes")
    if len(payload) > MAX_DOCUMENT_BYTES:
        raise MetricsError(
            f"observation document exceeds {MAX_DOCUMENT_BYTES} byte limit"
        )

    def reject_constant(value: str) -> None:
        raise MetricsError(f"non-finite JSON number is not allowed: {value}")

    try:
        return json.loads(
            payload.decode("utf-8"),
            object_pairs_hook=_reject_duplicate_keys,
            parse_constant=reject_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise MetricsError(f"malformed JSON: {exc}") from exc


def load_observation_file(path: str | Path) -> Mapping[str, Any]:
    file_path = Path(path)
    try:
        if file_path.stat().st_size > MAX_DOCUMENT_BYTES:
            raise MetricsError(
                f"observation document exceeds {MAX_DOCUMENT_BYTES} byte limit"
            )
        document = load_json_bytes(file_path.read_bytes())
    except OSError as exc:
        raise MetricsError(f"cannot read observation document: {exc}") from exc
    _require_mapping(document, "observation document")
    return document


def _require_mapping(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise MetricsError(f"{label} must be an object")
    return value


def _require_list(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise MetricsError(f"{label} must be an array")
    return value


def _reject_unknown(mapping: Mapping[str, Any], allowed: Iterable[str], label: str) -> None:
    if any(not isinstance(key, str) for key in mapping):
        raise MetricsError(f"{label} keys must be strings")
    allowed_set = set(allowed)
    unknown = sorted(set(mapping) - allowed_set)
    if unknown:
        raise MetricsError(f"unknown {label} field(s): {', '.join(unknown)}")


def _require_int(
    value: Any,
    label: str,
    *,
    minimum: int = 0,
    maximum: int | None = None,
) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise MetricsError(f"{label} must be an integer")
    if value < minimum:
        raise MetricsError(f"{label} must be an integer >= {minimum}")
    if maximum is not None and value > maximum:
        raise MetricsError(f"{label} must be an integer <= {maximum}")
    return value


def _require_number(value: Any, label: str, *, minimum: float = 0.0) -> int | float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise MetricsError(f"{label} must be a finite number")
    if isinstance(value, float) and not math.isfinite(value):
        raise MetricsError(f"{label} must be a finite number")
    numeric = value
    if numeric < minimum:
        raise MetricsError(f"{label} must be a finite number >= {minimum}")
    return float(value) if isinstance(value, float) else value


def _require_string(value: Any, label: str, *, pattern: str | None = None) -> str:
    if not isinstance(value, str) or not value:
        raise MetricsError(f"{label} must be a non-empty string")
    if pattern is not None and re.fullmatch(pattern, value) is None:
        raise MetricsError(f"{label} has an invalid format")
    return value


def _require_enum(value: Any, label: str, allowed: Sequence[str]) -> str:
    string = _require_string(value, label)
    if string not in allowed:
        raise MetricsError(f"{label} has an unsupported value: {string}")
    return string


def _require_bool(value: Any, label: str) -> bool:
    if not isinstance(value, bool):
        raise MetricsError(f"{label} must be boolean")
    return value


def _require_color(value: Any, label: str, *, nullable: bool = False) -> str | None:
    if value is None and nullable:
        return None
    return _require_string(value, label, pattern=r"#[0-9A-Fa-f]{6}")


def _load_contract() -> tuple[dict[str, list[str]], dict[str, Any], set[str]]:
    try:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        trace_schema = json.loads(TRACE_SCHEMA_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise MetricsError(f"cannot load LAB-10 contract: {exc}") from exc

    fields = manifest["metric_schema"]["fields"]
    field_groups = {group: list(names) for group, names in fields.items()}
    inventory = [name for names in field_groups.values() for name in names]
    if len(inventory) != len(set(inventory)):
        raise MetricsError("manifest metric inventory contains duplicate fields")
    if trace_schema.get("x-contract", {}).get("metric_inventory") != inventory:
        raise MetricsError("trace schema metric inventory does not match manifest")
    return field_groups, manifest["metric_schema"], set(inventory)


FIELD_GROUPS, METRIC_SCHEMA, METRIC_FIELDS = _load_contract()
METRIC_INVENTORY = [field for fields in FIELD_GROUPS.values() for field in fields]


def _unit_groups() -> dict[str, set[str]]:
    """Return the exact unit groups declared in the manifest.

    L10-07 adds these groups to the manifest instead of maintaining a second
    field-name contract in code.  The fallback below keeps an older checkout
    readable while still validating the complete known inventory.
    """

    declared = METRIC_SCHEMA.get("field_units")
    if isinstance(declared, Mapping):
        result: dict[str, set[str]] = {}
        for unit, names in declared.items():
            if not isinstance(names, list):
                raise MetricsError(f"manifest field_units.{unit} must be an array")
            result[unit] = set(names)
        declared_names = set().union(*result.values()) if result else set()
        if declared_names != METRIC_FIELDS:
            missing = sorted(METRIC_FIELDS - declared_names)
            extra = sorted(declared_names - METRIC_FIELDS)
            raise MetricsError(
                f"manifest field_units parity failure; missing={missing}, extra={extra}"
            )
        return result

    # This is only for the pre-L10-07 contract during a partial checkout.  The
    # committed manifest carries the explicit field_units object.
    result = {"count": set(), "us": set(), "bytes": set(), "percent": set(),
              "px": set(), "enum": set(), "boolean": set(), "version": set()}
    for field in METRIC_FIELDS:
        if field.endswith("_bytes"):
            result["bytes"].add(field)
        elif "_us" in field:
            result["us"].add(field)
        elif field.endswith("_percent"):
            result["percent"].add(field)
        elif field in {"final_logical_width", "final_logical_height"}:
            result["px"].add(field)
        elif field in {"terminal_outcome", "render_mode", "render_role",
                       "render_frame_phase", "render_lock_state",
                       "audio_continuity_status"}:
            result["enum"].add(field)
        elif field == "render_presentation_pending":
            result["boolean"].add(field)
        elif field == "final_format_version":
            result["version"].add(field)
        else:
            result["count"].add(field)
    return result


UNIT_GROUPS = _unit_groups()
FIELD_UNITS = {
    field: unit for unit, fields in UNIT_GROUPS.items() for field in fields
}

HARDWARE_ONLY_TASK_FIELDS = {
    "capture_worker_busy_us",
    "capture_worker_load_percent",
    "capture_worker_stack_high_water_bytes",
    "ui_task_busy_us",
    "ui_task_load_percent",
    "ui_task_stack_high_water_bytes",
    "audio_task_busy_us",
    "audio_task_load_percent",
    "audio_task_stack_high_water_bytes",
}
HARDWARE_ONLY_MEMORY_FIELDS = {
    "internal_free_bytes",
    "internal_min_free_bytes",
    "internal_largest_free_block_bytes",
    "psram_free_bytes",
    "psram_min_free_bytes",
    "psram_largest_free_block_bytes",
}
HARDWARE_ONLY_FIELDS = HARDWARE_ONLY_TASK_FIELDS | HARDWARE_ONLY_MEMORY_FIELDS

SIGNED_BYTE_FIELDS = {"memory_drift_bytes", "repeated_run_memory_drift_bytes"}


def _sample_series_contract() -> dict[str, tuple[str, ...]]:
    declared = METRIC_SCHEMA.get("sample_series")
    if not isinstance(declared, Mapping):
        return {
            base: tuple(f"{base}_{suffix}" for suffix in PERCENTILES)
            for base in ("touch_to_ink_us", "accepted_to_present_us", "ui_update_us",
                         "invalidation_us", "drawing_latency_us", "stop_cleanup_us",
                         "cancel_cleanup_us")
        }
    result: dict[str, tuple[str, ...]] = {}
    for base, outputs in declared.items():
        if not isinstance(base, str) or not isinstance(outputs, list):
            raise MetricsError("manifest sample_series entries must be arrays")
        if tuple(outputs) != tuple(f"{base}_{suffix}" for suffix in PERCENTILES):
            raise MetricsError(f"sample_series.{base} does not use declared percentile order")
        if any(output not in METRIC_FIELDS for output in outputs):
            raise MetricsError(f"sample_series.{base} references an unknown metric field")
        result[base] = tuple(outputs)
    return result


SAMPLE_SERIES = _sample_series_contract()
SAMPLE_OUTPUT_FIELDS = {field for fields in SAMPLE_SERIES.values() for field in fields}

_TRACE_ROOT_FIELDS = {
    "document", "trace_schema", "run_id", "lab_id", "lab_revision",
    "firmware_build_id", "session_generation", "workload", "workload_metadata",
    "evidence_kind", "clock", "bounds", "events", "terminal",
    "audio_concurrency", "metrics",
}
_TRACE_EVENT_FIELDS = {
    "event_id", "event_name", "t_us", "ideal_t_us", "timestamp_jitter_us",
    "provenance", "phase", "raw_touch_id", "raw_x", "raw_y", "display_x",
    "display_y", "logical_x", "logical_y", "accepted", "stroke_id", "color",
    "selected_color", "color_index", "boundary", "pause_before_us",
    "pen_up_gap_before_us", "reason", "outcome", "failure_reason", "duration_us",
    "update_kind", "render_mode", "render_role", "frame_phase", "lock_state",
    "points_processed", "raster_ops", "publication_ops", "backlog_points",
    "raster_budget", "publication_budget", "point_budget", "frame_generation",
    "presentation_pending", "invalidation_count", "full_screen_invalidation",
    "audio_continuity_status", "audio_occupancy_bytes", "audio_high_water_bytes",
    "resource", "bytes", "allocation_status", "cleanup_kind", "live_resources",
    "lost_event_count", "terminal_event_count",
}
_WORKLOAD_FIELDS = {
    "identity", "schema_version", "source", "evidence_provenance", "seed",
    "generator_version", "timestamp_jitter_bound_us", "declared_features",
}
_REPEATED_RUN_FIELDS = {
    "run_id", "provenance", "memory_before_bytes", "memory_after_bytes",
    "cleanup_time_us", "terminal_outcome", "live_resources_after_terminal",
}
_TRACE_REQUIRED_FIELDS = {
    "document", "trace_schema", "run_id", "lab_id", "lab_revision",
    "firmware_build_id", "session_generation", "workload", "workload_metadata",
    "evidence_kind", "clock", "bounds", "events", "terminal",
}
_TRACE_EVENT_REQUIRED_FIELDS = {
    "capture_started": set(),
    "touch_report": {"raw_touch_id", "raw_x", "raw_y", "display_x", "display_y", "accepted", "stroke_id", "color"},
    "touch_accepted": {"raw_touch_id", "raw_x", "raw_y", "display_x", "display_y", "logical_x", "logical_y", "accepted", "stroke_id", "color"},
    "touch_rejected": {"raw_touch_id", "raw_x", "raw_y", "display_x", "display_y", "accepted", "stroke_id", "color", "reason"},
    "point_coalesced": {"raw_touch_id", "raw_x", "raw_y", "display_x", "display_y", "accepted", "stroke_id", "color", "reason"},
    "palette_changed": {"selected_color"},
    "stroke_started": {"stroke_id", "color"},
    "stroke_ended": {"stroke_id"},
    "ui_update": {"duration_us", "update_kind", "invalidation_count"},
    "render_update": {"render_mode", "render_role", "frame_phase", "lock_state", "points_processed", "raster_ops", "publication_ops", "backlog_points", "raster_budget", "publication_budget", "point_budget", "frame_generation", "invalidation_count", "full_screen_invalidation", "presentation_pending"},
    "audio_health": {"audio_continuity_status", "audio_occupancy_bytes", "audio_high_water_bytes"},
    "allocation": {"resource", "bytes", "allocation_status"},
    "cleanup": {"cleanup_kind", "duration_us", "live_resources"},
    "terminal": {"outcome", "terminal_event_count"},
    "trace_loss": {"reason", "lost_event_count"},
}

TRACE_EVENT_NAMES = (
    "capture_started", "touch_report", "touch_accepted", "touch_rejected",
    "point_coalesced", "palette_changed", "stroke_started", "stroke_ended",
    "ui_update", "render_update", "audio_health", "allocation", "cleanup",
    "terminal", "trace_loss",
)
TRACE_PHASES = ("NONE", "DOWN", "MOVE", "UP", "LEAVE", "PALETTE_CHANGE", "TERMINAL")
TRACE_PROVENANCES = PROVENANCE_VALUES
TRACE_OUTCOMES = (
    "completed", "stopped", "cancelled", "duration_cap", "point_cap",
    "queue_overflow", "failed",
)
TRACE_BOUNDARIES = (
    "NONE", "STROKE_START", "STROKE_CONTINUE", "STROKE_END", "PEN_UP_GAP",
    "COLOR_CHANGE", "TERMINAL",
)
TRACE_TOUCH_EVENT_NAMES = ("touch_report", "touch_accepted", "touch_rejected", "point_coalesced")
TRACE_UPDATE_KINDS = ("ink", "palette", "state", "cleanup")
TRACE_RENDER_MODES = ("direct_point", "segment", "bounded_batch", "whole_frame")
TRACE_RENDER_ROLES = ("control", "candidate")
TRACE_FRAME_PHASES = ("idle", "clear", "replay", "publish")
TRACE_LOCK_STATES = ("unknown", "ui_task_owned", "acquired", "released", "failed")
TRACE_AUDIO_STATUSES = ("continuous", "overrun", "underrun", "not_measured", "blocked")
TRACE_RESOURCES = ("sketch", "framebuffer", "pcm", "queue", "worker", "ui")
TRACE_ALLOCATION_STATUSES = ("attempted", "succeeded", "failed", "released")
TRACE_CLEANUP_KINDS = ("stop", "cancel", "terminal", "repeated_run")
TRACE_WORKLOAD_FEATURES = (
    "variable_spacing", "variable_velocity", "corners", "pauses", "pen_up_gaps",
    "multiple_strokes", "color_changes", "edge_points", "leave_reenter",
    "background_erase", "stop", "cancel",
)

METRIC_ENUM_VALUES = {
    "terminal_outcome": TRACE_OUTCOMES,
    "render_mode": TRACE_RENDER_MODES,
    "render_role": TRACE_RENDER_ROLES,
    "render_frame_phase": TRACE_FRAME_PHASES,
    "render_lock_state": TRACE_LOCK_STATES,
    "audio_continuity_status": (
        "continuous", "overrun", "underrun", "not_measured", "blocked",
        "model-continuous", "model-overrun", "model-underrun",
        "model-overrun-and-underrun",
    ),
}


def percentile_nearest_rank(values: Sequence[int | float], percentile: int) -> int | float:
    """Return nearest-rank percentile with deterministic ceil semantics.

    Samples are sorted numerically.  For ``n`` real samples and percentile
    ``p``, the one-based rank is ``ceil(p*n/100)`` clamped to ``[1,n]``.
    Thus p50 for ``[1, 2, 3, 4]`` is 2, p95 is 4, and p99 is 4.  Empty input
    is not a numeric zero; callers must emit a missing record.
    """

    if isinstance(values, (str, bytes)) or not isinstance(values, Sequence):
        raise MetricsError("percentile samples must be a sequence")
    if percentile not in (50, 95, 99):
        raise MetricsError("percentile must be one of 50, 95, or 99")
    if not values:
        raise MetricsError("cannot compute a percentile from empty samples")
    if any(
        isinstance(value, bool)
        or not isinstance(value, (int, float))
        or (isinstance(value, float) and not math.isfinite(value))
        for value in values
    ):
        raise MetricsError("percentile samples must be finite numbers")
    ordered = sorted(values)
    rank = max(1, math.ceil(percentile * len(ordered) / 100))
    return ordered[rank - 1]


def _record(
    value: Any,
    field: str,
    provenance: str,
    sample_count: int,
    missing_reason: str | None = None,
) -> MetricRecord:
    if field not in METRIC_FIELDS:
        raise MetricsError(f"unknown metric field: {field}")
    _require_enum(provenance, f"{field}.provenance", PROVENANCE_VALUES)
    sample_count = _require_int(
        sample_count, f"{field}.sample_count", maximum=MAX_TOTAL_SAMPLES
    )
    if missing_reason is not None:
        _require_string(missing_reason, f"{field}.missing_reason")

    must_be_missing = provenance == "unmeasured/blocked"
    if field in HARDWARE_ONLY_FIELDS and provenance in {"host", "derived_static"}:
        must_be_missing = True
    if must_be_missing and (value is not None or sample_count != 0 or not missing_reason):
        raise MetricsError(
            f"{field} is unmeasured/blocked and requires value=null, "
            "sample_count=0, and missing_reason"
        )
    if value is None:
        if sample_count != 0 or not missing_reason:
            raise MetricsError(
                f"missing metric {field} requires sample_count=0 and missing_reason"
            )
    elif sample_count == 0:
        raise MetricsError(f"present metric {field} requires sample_count > 0")
    elif missing_reason is not None:
        raise MetricsError(f"present metric {field} cannot have missing_reason")
    return MetricRecord(value, FIELD_UNITS[field], provenance, sample_count, missing_reason)


def _missing_record(field: str, reason: str = MISSING_NO_OBSERVATION) -> MetricRecord:
    if field in HARDWARE_ONLY_TASK_FIELDS:
        reason = MISSING_HARDWARE_TASK
    elif field in HARDWARE_ONLY_MEMORY_FIELDS:
        reason = MISSING_HARDWARE_MEMORY
    return _record(None, field, "unmeasured/blocked", 0, reason)


def _field_provenance(wrapper: Mapping[str, Any], default: str, field: str) -> str:
    provenance = wrapper.get("provenance", default)
    return _require_enum(provenance, f"{field}.provenance", PROVENANCE_VALUES)


def _validate_unit(wrapper: Mapping[str, Any], field: str) -> None:
    unit = wrapper.get("unit")
    expected = FIELD_UNITS[field]
    if unit is not None and unit != expected:
        raise MetricsError(f"unit mismatch for {field}: expected {expected}, got {unit}")


def _validate_value(value: Any, field: str) -> Any:
    if field not in METRIC_FIELDS:
        raise MetricsError(f"unknown metric field: {field}")
    unit = FIELD_UNITS[field]
    if value is None:
        return None
    if unit in {"count", "bytes", "us", "px", "generation", "frames", "samples"}:
        if field in SIGNED_BYTE_FIELDS:
            return _require_int(value, field, minimum=-2**63, maximum=2**63 - 1)
        maximum = UINT32_MAX if unit == "generation" else UINT64_MAX
        return _require_int(value, field, maximum=maximum)
    if unit == "percent":
        number = _require_number(value, field)
        if number > 100:
            raise MetricsError(f"{field} percent cannot exceed 100")
        return int(value) if isinstance(value, int) else number
    if unit == "boolean":
        if not isinstance(value, bool):
            raise MetricsError(f"{field} must be boolean")
        return value
    if unit in {"enum", "version"}:
        string = _require_string(value, field)
        allowed = METRIC_ENUM_VALUES.get(field)
        if allowed is not None and string not in allowed:
            raise MetricsError(f"{field} has an unsupported value: {string}")
        return string
    raise MetricsError(f"unsupported unit for {field}: {unit}")


def _validate_workload(workload: Mapping[str, Any]) -> dict[str, Any]:
    _reject_unknown(workload, _WORKLOAD_FIELDS, "workload")
    required_fields = (
        "identity", "schema_version", "source", "evidence_provenance", "seed",
        "generator_version", "timestamp_jitter_bound_us", "declared_features",
    )
    for required in required_fields:
        if required not in workload:
            raise MetricsError(f"workload missing {required}")

    source = _require_enum(workload["source"], "workload.source", ("manual", "synthetic"))
    _require_string(workload["schema_version"], "workload.schema_version")
    if workload["schema_version"] != "lab10-workload/v1":
        raise MetricsError("unsupported workload schema version")
    _require_enum(workload["evidence_provenance"], "workload.evidence_provenance", PROVENANCE_VALUES)
    generator_version = _require_string(workload["generator_version"], "workload.generator_version")
    timestamp_jitter_bound = _require_int(
        workload["timestamp_jitter_bound_us"],
        "workload.timestamp_jitter_bound_us",
        maximum=2000,
    )
    declared_features = _require_list(workload["declared_features"], "workload.declared_features")
    if not declared_features or len(declared_features) > len(TRACE_WORKLOAD_FEATURES):
        raise MetricsError("workload.declared_features must contain 1..12 features")
    normalized_features = []
    for index, feature in enumerate(declared_features):
        normalized_features.append(
            _require_enum(feature, f"workload.declared_features[{index}]", TRACE_WORKLOAD_FEATURES)
        )
    if len(set(normalized_features)) != len(normalized_features):
        raise MetricsError("workload.declared_features must not contain duplicates")

    identity_pattern = r"^synthetic-[A-Za-z0-9._-]+-v1$" if source == "synthetic" else r"^manual-[A-Za-z0-9._-]+$"
    _require_string(workload["identity"], "workload.identity", pattern=identity_pattern)
    seed = workload["seed"]
    if seed is not None:
        _require_int(seed, "workload.seed", maximum=UINT64_MAX)

    if source == "synthetic":
        if generator_version != "synthetic-human-like-v1":
            raise MetricsError("synthetic workload requires synthetic-human-like-v1")
        _require_int(seed, "workload.seed", minimum=1, maximum=UINT64_MAX)
        if timestamp_jitter_bound != 2000:
            raise MetricsError("synthetic workload requires a 2000 us jitter bound")
        if workload["evidence_provenance"] != "host":
            raise MetricsError("synthetic workload provenance must be host")
    else:
        if seed is not None:
            raise MetricsError("manual workload seed must be null")
        if generator_version != "not_applicable":
            raise MetricsError("manual workload generator_version must be not_applicable")
        if timestamp_jitter_bound != 0:
            raise MetricsError("manual workload jitter bound must be zero")
    return copy.deepcopy(dict(workload))


def _validate_trace_event(event: Mapping[str, Any], index: int) -> str:
    label = f"trace.events[{index}]"
    _reject_unknown(event, _TRACE_EVENT_FIELDS, label)
    for required in ("event_id", "event_name", "t_us", "provenance", "phase"):
        if required not in event:
            raise MetricsError(f"{label} missing {required}")

    event_id = _require_string(event["event_id"], f"{label}.event_id", pattern=r"e-[0-9]+")
    event_name = _require_enum(event["event_name"], f"{label}.event_name", TRACE_EVENT_NAMES)
    _require_int(event["t_us"], f"{label}.t_us", maximum=UINT64_MAX)
    _require_enum(event["provenance"], f"{label}.provenance", TRACE_PROVENANCES)
    phase = _require_enum(event["phase"], f"{label}.phase", TRACE_PHASES)

    if "ideal_t_us" in event:
        _require_int(event["ideal_t_us"], f"{label}.ideal_t_us", maximum=UINT64_MAX)
    if "timestamp_jitter_us" in event:
        _require_int(event["timestamp_jitter_us"], f"{label}.timestamp_jitter_us", minimum=-2000, maximum=2000)
    for coordinate in ("raw_x", "raw_y", "display_x", "display_y"):
        if coordinate in event:
            _require_int(event[coordinate], f"{label}.{coordinate}", minimum=-32768, maximum=32767)
    if "raw_touch_id" in event:
        if event["raw_touch_id"] is not None:
            _require_int(event["raw_touch_id"], f"{label}.raw_touch_id", maximum=UINT8_MAX)
    for coordinate in ("logical_x", "logical_y"):
        if coordinate in event and event[coordinate] is not None:
            _require_int(event[coordinate], f"{label}.{coordinate}", maximum=119)
    if "accepted" in event:
        _require_bool(event["accepted"], f"{label}.accepted")
    if "stroke_id" in event and event["stroke_id"] is not None:
        _require_int(event["stroke_id"], f"{label}.stroke_id", minimum=1, maximum=UINT32_MAX)
    if "color" in event:
        _require_color(event["color"], f"{label}.color", nullable=True)
    if "selected_color" in event:
        _require_color(event["selected_color"], f"{label}.selected_color")
    if "color_index" in event:
        _require_int(event["color_index"], f"{label}.color_index", maximum=5)
    if "boundary" in event:
        _require_enum(event["boundary"], f"{label}.boundary", TRACE_BOUNDARIES)
    for field in ("pause_before_us", "pen_up_gap_before_us"):
        if field in event:
            _require_int(event[field], f"{label}.{field}", maximum=UINT32_MAX)
    for field in ("reason", "failure_reason"):
        if field in event:
            _require_string(event[field], f"{label}.{field}")
    if "outcome" in event:
        _require_enum(event["outcome"], f"{label}.outcome", TRACE_OUTCOMES)
    if "duration_us" in event:
        _require_int(event["duration_us"], f"{label}.duration_us", maximum=UINT64_MAX)
    if "update_kind" in event:
        _require_enum(event["update_kind"], f"{label}.update_kind", TRACE_UPDATE_KINDS)
    if "render_mode" in event:
        _require_enum(event["render_mode"], f"{label}.render_mode", TRACE_RENDER_MODES)
    if "render_role" in event:
        _require_enum(event["render_role"], f"{label}.render_role", TRACE_RENDER_ROLES)
    if "frame_phase" in event:
        _require_enum(event["frame_phase"], f"{label}.frame_phase", TRACE_FRAME_PHASES)
    if "lock_state" in event:
        _require_enum(event["lock_state"], f"{label}.lock_state", TRACE_LOCK_STATES)
    for field in ("points_processed", "raster_ops", "publication_ops", "backlog_points", "invalidation_count", "audio_occupancy_bytes", "audio_high_water_bytes", "bytes", "live_resources", "lost_event_count"):
        if field in event:
            _require_int(event[field], f"{label}.{field}", maximum=UINT64_MAX)
    if "raster_budget" in event:
        _require_int(event["raster_budget"], f"{label}.raster_budget", minimum=1, maximum=512)
    if "publication_budget" in event:
        _require_int(event["publication_budget"], f"{label}.publication_budget", minimum=1, maximum=512)
    if "point_budget" in event:
        _require_int(event["point_budget"], f"{label}.point_budget", minimum=1, maximum=64)
    if "frame_generation" in event:
        _require_int(event["frame_generation"], f"{label}.frame_generation", maximum=UINT32_MAX)
    for field in ("presentation_pending", "full_screen_invalidation"):
        if field in event:
            _require_bool(event[field], f"{label}.{field}")
    if "terminal_event_count" in event:
        _require_int(event["terminal_event_count"], f"{label}.terminal_event_count", minimum=1, maximum=1)
    if event_name == "trace_loss":
        _require_int(event["lost_event_count"], f"{label}.lost_event_count", minimum=1, maximum=UINT64_MAX)
    if "audio_continuity_status" in event:
        _require_enum(event["audio_continuity_status"], f"{label}.audio_continuity_status", TRACE_AUDIO_STATUSES)
    if "resource" in event:
        _require_enum(event["resource"], f"{label}.resource", TRACE_RESOURCES)
    if "allocation_status" in event:
        _require_enum(event["allocation_status"], f"{label}.allocation_status", TRACE_ALLOCATION_STATUSES)
    if "cleanup_kind" in event:
        _require_enum(event["cleanup_kind"], f"{label}.cleanup_kind", TRACE_CLEANUP_KINDS)

    required_event_fields = _TRACE_EVENT_REQUIRED_FIELDS[event_name]
    missing_event = sorted(required_event_fields - set(event))
    if missing_event:
        raise MetricsError(f"{label} missing required field(s): {', '.join(missing_event)}")

    if event_name == "capture_started" and phase != "NONE":
        raise MetricsError(f"{label}.phase must be NONE for capture_started")
    if event_name in TRACE_TOUCH_EVENT_NAMES:
        if phase not in ("DOWN", "MOVE", "UP", "LEAVE"):
            raise MetricsError(f"{label}.phase is invalid for a touch event")
    if event_name == "touch_accepted":
        if event["accepted"] is not True or phase not in ("DOWN", "MOVE"):
            raise MetricsError(f"{label} must declare accepted=true with DOWN or MOVE")
    if event_name in {"touch_rejected", "point_coalesced"} and event["accepted"] is not False:
        raise MetricsError(f"{label} must declare accepted=false")
    if event_name == "point_coalesced" and (phase != "MOVE" or event["reason"] != "coalesced"):
        raise MetricsError(f"{label} must declare phase=MOVE and reason=coalesced")
    if event_name == "palette_changed" and phase != "PALETTE_CHANGE":
        raise MetricsError(f"{label}.phase must be PALETTE_CHANGE")
    if event_name == "stroke_started" and phase != "DOWN":
        raise MetricsError(f"{label}.phase must be DOWN")
    if event_name == "stroke_ended" and phase not in ("UP", "LEAVE"):
        raise MetricsError(f"{label}.phase must be UP or LEAVE")
    if event_name in {"ui_update", "render_update", "audio_health", "allocation", "cleanup", "trace_loss"} and phase != "NONE":
        raise MetricsError(f"{label}.phase must be NONE")
    if event_name == "terminal" and phase != "TERMINAL":
        raise MetricsError(f"{label}.phase must be TERMINAL")
    return event_id


def _validate_trace(trace: Mapping[str, Any], default_provenance: str) -> tuple[list[Mapping[str, Any]], str]:
    _reject_unknown(trace, _TRACE_ROOT_FIELDS, "trace")
    missing_root = sorted(_TRACE_REQUIRED_FIELDS - set(trace))
    if missing_root:
        raise MetricsError("trace missing required field(s): " + ", ".join(missing_root))
    if _require_string(trace["document"], "trace.document") != "family-link.lab10-record-trace/v1":
        raise MetricsError("unsupported trace document")
    if _require_string(trace["trace_schema"], "trace.trace_schema") != "lab10-record-trace/v1":
        raise MetricsError("unsupported trace schema")
    if _require_string(trace["lab_id"], "trace.lab_id") != "LAB-10":
        raise MetricsError("trace.lab_id must be LAB-10")
    _require_string(trace["run_id"], "trace.run_id", pattern=r"[A-Za-z0-9][A-Za-z0-9._-]*")
    _require_string(trace["lab_revision"], "trace.lab_revision")
    _require_string(trace["firmware_build_id"], "trace.firmware_build_id")
    _require_int(
        trace["session_generation"],
        "trace.session_generation",
        minimum=1,
        maximum=VALID_SESSION_GENERATION_MAX,
    )
    trace_workload = _require_enum(trace["workload"], "trace.workload", ("manual", "synthetic-human-like-v1"))
    workload_metadata = _validate_workload(_require_mapping(trace["workload_metadata"], "trace.workload_metadata"))
    expected_workload = "synthetic-human-like-v1" if workload_metadata["source"] == "synthetic" else "manual"
    if trace_workload != expected_workload:
        raise MetricsError("trace.workload does not match workload_metadata.source")

    trace_provenance = _require_enum(trace["evidence_kind"], "trace.evidence_kind", PROVENANCE_VALUES)
    clock = _require_mapping(trace["clock"], "trace.clock")
    _reject_unknown(clock, {"source", "unit", "timestamp_jitter_bound_us"}, "trace.clock")
    for required in ("source", "unit", "timestamp_jitter_bound_us"):
        if required not in clock:
            raise MetricsError(f"trace.clock missing {required}")
    clock_source = _require_enum(clock["source"], "trace.clock.source", ("monotonic_device", "monotonic_host"))
    if _require_string(clock["unit"], "trace.clock.unit") != "us":
        raise MetricsError("trace.clock.unit must be us")
    clock_jitter = _require_int(clock["timestamp_jitter_bound_us"], "trace.clock.timestamp_jitter_bound_us", maximum=2000)
    expected_jitter = 0 if clock_source == "monotonic_device" else 2000
    if clock_jitter != expected_jitter:
        raise MetricsError("trace.clock.timestamp_jitter_bound_us does not match clock source")

    bounds = _require_mapping(trace["bounds"], "trace.bounds")
    expected_bounds = {
        "lcd_width_px": 320, "lcd_height_px": 240, "canvas_width_px": 240,
        "canvas_height_px": 240, "rail_origin_x_px": 240, "logical_width": 120,
        "logical_height": 120, "duration_cap_ms": 180000, "accepted_point_cap": 4096,
        "queue_capacity": 16, "minimum_move_distance_px": 2,
    }
    _reject_unknown(bounds, expected_bounds, "trace.bounds")
    missing_bounds = sorted(set(expected_bounds) - set(bounds))
    if missing_bounds:
        raise MetricsError("trace.bounds missing required field(s): " + ", ".join(missing_bounds))
    for field, expected in expected_bounds.items():
        _require_int(bounds[field], f"trace.bounds.{field}", maximum=UINT32_MAX)
        if bounds[field] != expected:
            raise MetricsError("trace.bounds does not match the LAB-10 fixed contract")

    events = _require_list(trace["events"], "trace.events")
    if not events or len(events) > MAX_TRACE_EVENTS:
        raise MetricsError(f"trace.events must contain 1..{MAX_TRACE_EVENTS} events")
    previous_t = -1
    event_ids: set[str] = set()
    terminal_indices: list[int] = []
    for index, raw_event in enumerate(events):
        event = _require_mapping(raw_event, f"trace.events[{index}]")
        event_id = _validate_trace_event(event, index)
        if event_id in event_ids:
            raise MetricsError(f"duplicate trace event_id: {event_id}")
        event_ids.add(event_id)
        timestamp = event["t_us"]
        if timestamp < previous_t:
            raise MetricsError("trace event timestamps must be non-decreasing")
        previous_t = timestamp
        if event["event_name"] == "terminal":
            terminal_indices.append(index)
        if terminal_indices and index > terminal_indices[0]:
            raise MetricsError("trace contains an event after terminalization")

    if len(terminal_indices) != 1:
        raise MetricsError("trace must contain exactly one terminal event")
    terminal = _require_mapping(trace["terminal"], "trace.terminal")
    _reject_unknown(terminal, {"event_id", "outcome", "event_count"}, "trace.terminal")
    for required in ("event_id", "outcome", "event_count"):
        if required not in terminal:
            raise MetricsError(f"trace.terminal missing {required}")
    terminal_id = _require_string(terminal["event_id"], "trace.terminal.event_id", pattern=r"e-[0-9]+")
    terminal_outcome = _require_enum(terminal["outcome"], "trace.terminal.outcome", TRACE_OUTCOMES)
    _require_int(terminal["event_count"], "trace.terminal.event_count", minimum=1, maximum=1)
    terminal_event = events[terminal_indices[0]]
    if terminal_id != terminal_event["event_id"]:
        raise MetricsError("trace terminal summary does not identify terminal event")
    if terminal_outcome != terminal_event["outcome"]:
        raise MetricsError("trace terminal outcome mismatch")
    _validate_optional_trace_metrics(trace)
    return [dict(event) for event in events], trace_provenance


def _validate_optional_trace_metrics(trace: Mapping[str, Any]) -> None:
    if "metrics" not in trace:
        return
    metrics = _require_mapping(trace["metrics"], "trace.metrics")
    _reject_unknown(metrics, METRIC_FIELDS, "trace.metrics")
    for field, raw_record in metrics.items():
        record = _require_mapping(raw_record, f"trace.metrics.{field}")
        _reject_unknown(
            record,
            {"value", "unit", "provenance", "sample_count", "missing_reason"},
            f"trace.metrics.{field}",
        )
        for required in ("value", "unit", "provenance", "sample_count", "missing_reason"):
            if required not in record:
                raise MetricsError(f"trace.metrics.{field} missing {required}")
        _validate_unit(record, field)
        provenance = _require_enum(record["provenance"], f"trace.metrics.{field}.provenance", PROVENANCE_VALUES)
        sample_count = _require_int(record["sample_count"], f"trace.metrics.{field}.sample_count", maximum=MAX_TOTAL_SAMPLES)
        value = _validate_value(record["value"], field)
        reason = record["missing_reason"]
        if reason is not None:
            _require_string(reason, f"trace.metrics.{field}.missing_reason")
        _record(value, field, provenance, sample_count, reason)


def _event_provenance(events: Sequence[Mapping[str, Any]], selected: Sequence[Mapping[str, Any]], fallback: str) -> str:
    provenances = {event.get("provenance", fallback) for event in selected}
    if len(provenances) > 1:
        raise MetricsError("one metric cannot combine observations with mixed provenance")
    return next(iter(provenances), fallback)


def _trace_field_observations(
    trace: Mapping[str, Any], events: Sequence[Mapping[str, Any]], trace_provenance: str
) -> tuple[dict[str, Any], dict[str, list[Any]]]:
    """Derive only facts represented by the trace; absence remains missing."""

    scalar_values: dict[str, Any] = {}
    samples: dict[str, list[Any]] = {}

    def scalar(field: str, value: Any, selected: Sequence[Mapping[str, Any]]) -> None:
        scalar_values[field] = {
            "value": value,
            "provenance": _event_provenance(events, selected, trace_provenance),
        }

    def series(base: str, values: list[Any], selected: Sequence[Mapping[str, Any]]) -> None:
        if values:
            samples[base] = {
                "samples": values,
                "provenance": _event_provenance(events, selected, trace_provenance),
            }

    by_name = {name: [event for event in events if event.get("event_name") == name]
               for name in {
                   "touch_report", "touch_accepted", "touch_rejected", "point_coalesced",
                   "palette_changed", "stroke_started", "stroke_ended", "render_update", "ui_update",
                   "audio_health", "cleanup", "trace_loss", "terminal",
               }}
    # The validator requires touch_accepted.accepted=true.  Keep the predicate
    # at the aggregation boundary as well so a future event extension cannot
    # turn a false-valued record into an accepted-point count.
    accepted = [event for event in by_name["touch_accepted"] if event.get("accepted") is True]
    rejected = by_name["touch_rejected"]
    reports = by_name["touch_report"]
    coalesced = by_name["point_coalesced"]
    palette = by_name["palette_changed"]
    strokes = by_name["stroke_started"]
    stroke_ends = by_name["stroke_ended"]
    terminal = by_name["terminal"]
    loss = by_name["trace_loss"]
    count_selected = reports + accepted + rejected + coalesced + palette + strokes + terminal + loss

    # A complete validated trace is a real observation of zero for these
    # terminal/loss counters; missing traces are handled outside this function.
    scalar("raw_report_count", len(reports), reports or events)
    scalar("accepted_point_count", len(accepted), accepted or events)
    scalar("coalesced_move_count", len(coalesced), coalesced or events)
    scalar("rejected_report_count", len(rejected), rejected or events)
    scalar("rejected_rail_touch_count", sum(_reason_matches(e, "rail") for e in rejected), rejected or events)
    scalar("rejected_outside_display_count", sum(_reason_matches(e, "outside") for e in rejected), rejected or events)
    scalar("rejected_second_touch_count", sum(_reason_matches(e, "second") for e in rejected), rejected or events)
    scalar("duplicate_point_count", sum(_reason_matches(e, "duplicate") for e in rejected + coalesced), rejected + coalesced or events)
    scalar("edge_point_count", sum(_is_edge_event(e) for e in accepted), accepted or events)
    scalar("stroke_count", len(strokes), strokes or events)
    scalar("color_change_count", len(palette), palette or events)
    scalar("palette_hit_count", len(palette), palette or events)
    scalar("final_stroke_count", len(strokes), strokes or events)
    scalar("final_color_change_count", len(palette), palette or events)
    scalar(
        "pen_up_boundary_count",
        len(stroke_ends) + sum(event.get("boundary") == "PEN_UP_GAP" for event in accepted),
        stroke_ends + accepted or events,
    )
    scalar("queue_overflow_count", sum(_reason_matches(e, "queue") for e in loss), loss or events)
    terminal_outcome = terminal[0].get("outcome") if terminal else None
    if terminal_outcome is not None:
        scalar("point_cap_hit_count", int(terminal_outcome == "point_cap"), terminal)
        scalar("duration_cap_hit_count", int(terminal_outcome == "duration_cap"), terminal)
        scalar("terminal_event_count", len(terminal), terminal)
        scalar("terminal_outcome", terminal_outcome, terminal)
    scalar("trace_loss_count", sum(e.get("lost_event_count", 0) for e in loss), loss or events)

    # Capture queue depths may be emitted as explicit observation fields; the
    # trace event contract does not carry them, so do not infer them here.
    ui_updates = by_name["ui_update"]
    series("ui_update_us", [e["duration_us"] for e in ui_updates if "duration_us" in e], ui_updates)
    scalar("invalidation_count", sum(e.get("invalidation_count", 0) for e in ui_updates), ui_updates or events)

    render_updates = by_name["render_update"]
    if render_updates:
        last_render = render_updates[-1]
        scalar("render_mode", last_render["render_mode"], render_updates)
        scalar("render_role", last_render["render_role"], render_updates)
        scalar("render_update_count", len(render_updates), render_updates)
        scalar("render_pending_point_count", last_render["backlog_points"], render_updates)
        scalar("render_backlog_high_water", max(e["backlog_points"] for e in render_updates), render_updates)
        scalar("render_raster_ops_last_update", last_render["raster_ops"], render_updates)
        scalar("render_raster_ops_high_water", max(e["raster_ops"] for e in render_updates), render_updates)
        scalar("render_publication_ops_last_update", last_render["publication_ops"], render_updates)
        scalar("render_publication_ops_high_water", max(e["publication_ops"] for e in render_updates), render_updates)
        scalar("render_invalidation_count", max(e["invalidation_count"] for e in render_updates), render_updates)
        scalar("render_full_screen_invalidation_count", sum(bool(e["full_screen_invalidation"]) for e in render_updates), render_updates)
        scalar("render_frame_generation", last_render["frame_generation"], render_updates)
        scalar("render_frame_phase", last_render["frame_phase"], render_updates)
        scalar("render_presentation_pending", last_render["presentation_pending"], render_updates)
        scalar("render_lock_state", last_render["lock_state"], render_updates)

    audio_updates = by_name["audio_health"]
    if audio_updates:
        last_audio = audio_updates[-1]
        scalar("audio_continuity_status", last_audio["audio_continuity_status"], audio_updates)
        scalar("audio_buffer_occupancy_bytes", last_audio["audio_occupancy_bytes"], audio_updates)
        scalar("audio_buffer_high_water_bytes", max(e["audio_high_water_bytes"] for e in audio_updates), audio_updates)

    cleanups = by_name["cleanup"]
    if cleanups:
        scalar("cleanup_time_us", cleanups[-1]["duration_us"], cleanups)
        scalar("live_resources_after_terminal", cleanups[-1]["live_resources"], cleanups)
        stop = [e for e in cleanups if e.get("cleanup_kind") == "stop"]
        cancel = [e for e in cleanups if e.get("cleanup_kind") == "cancel"]
        series("stop_cleanup_us", [e["duration_us"] for e in stop], stop)
        series("cancel_cleanup_us", [e["duration_us"] for e in cancel], cancel)

    # Keep the unused local explicit: it documents that no other count is
    # inferred merely because the trace was valid.
    _ = count_selected
    return scalar_values, samples


def _reason_matches(event: Mapping[str, Any], token: str) -> bool:
    reason = event.get("reason", "")
    if not isinstance(reason, str):
        raise MetricsError("trace event reason must be a string")
    reason = reason.lower()
    return token in reason


def _is_edge_event(event: Mapping[str, Any]) -> bool:
    return any(event.get(key) in (0, 239) for key in ("display_x", "display_y"))


def _parse_explicit_fields(
    fields: Mapping[str, Any], default_provenance: str
) -> tuple[dict[str, MetricRecord], dict[str, tuple[list[int | float], str, str | None]]]:
    records: dict[str, MetricRecord] = {}
    series: dict[str, tuple[list[int | float], str, str | None]] = {}
    allowed_input_fields = (METRIC_FIELDS - SAMPLE_OUTPUT_FIELDS) | set(SAMPLE_SERIES)
    unknown = sorted(set(fields) - allowed_input_fields)
    if unknown:
        raise MetricsError(
            "unknown observation field(s): " + ", ".join(unknown)
        )
    total_samples = 0
    for field, raw_wrapper in fields.items():
        wrapper = _require_mapping(raw_wrapper, f"fields.{field}")
        _reject_unknown(wrapper, {"value", "samples", "unit", "provenance", "missing_reason"}, f"fields.{field}")
        _validate_unit(wrapper, field if field in METRIC_FIELDS else SAMPLE_SERIES[field][0])
        provenance = _field_provenance(wrapper, default_provenance, field)
        has_value = "value" in wrapper
        has_samples = "samples" in wrapper
        if has_value == has_samples:
            if has_value:
                raise MetricsError(f"fields.{field} must contain exactly one of value or samples")
            if provenance != "unmeasured/blocked" and not (
                field in HARDWARE_ONLY_FIELDS and provenance in {"host", "derived_static"}
            ):
                raise MetricsError(f"fields.{field} must contain exactly one of value or samples")
        missing_reason = wrapper.get("missing_reason")
        if missing_reason is not None and (not isinstance(missing_reason, str) or not missing_reason):
            raise MetricsError(f"fields.{field}.missing_reason must be a non-empty string")
        missing_reason_supplied = missing_reason is not None

        if not has_value and not has_samples:
            if not missing_reason:
                raise MetricsError(f"missing field {field} requires missing_reason")
            if field in SAMPLE_SERIES:
                series[field] = ([], provenance, missing_reason)
            else:
                records[field] = _record(None, field, provenance, 0, missing_reason)
            continue

        if has_samples:
            if field not in SAMPLE_SERIES:
                raise MetricsError(f"{field} is not a percentile sample series")
            values = _require_list(wrapper["samples"], f"fields.{field}.samples")
            if len(values) > MAX_SAMPLES_PER_SERIES:
                raise MetricsError(f"fields.{field}.samples exceeds {MAX_SAMPLES_PER_SERIES}")
            total_samples += len(values)
            if total_samples > MAX_TOTAL_SAMPLES:
                raise MetricsError(
                    f"observation samples exceed {MAX_TOTAL_SAMPLES} total values"
                )
            numeric_values: list[int | float] = []
            for index, value in enumerate(values):
                numeric = _require_number(value, f"fields.{field}.samples[{index}]")
                numeric_values.append(int(value) if isinstance(value, int) else numeric)
            if not values and not missing_reason:
                if (
                    not missing_reason_supplied
                    and (provenance == "unmeasured/blocked" or (
                        field in HARDWARE_ONLY_FIELDS and provenance in {"host", "derived_static"}
                    ))
                ):
                    raise MetricsError(f"missing field {field} requires missing_reason")
                missing_reason = MISSING_NO_SAMPLES
            if values and missing_reason is not None:
                raise MetricsError(f"present sample series {field} cannot have missing_reason")
            if provenance == "unmeasured/blocked" or (
                field in HARDWARE_ONLY_FIELDS and provenance in {"host", "derived_static"}
            ):
                if numeric_values or not missing_reason:
                    raise MetricsError(
                        f"{field} is unmeasured/blocked and cannot contain numeric samples"
                    )
            series[field] = (numeric_values, provenance, missing_reason)
            continue

        if field in SAMPLE_SERIES:
            if wrapper["value"] is not None:
                raise MetricsError(f"{field} is a sample series and requires samples")
            if not missing_reason:
                raise MetricsError(f"missing sample series {field} requires missing_reason")
            series[field] = ([], provenance, missing_reason)
            continue

        value = _validate_value(wrapper["value"], field)
        if value is None:
            if not missing_reason:
                raise MetricsError(f"missing field {field} requires missing_reason")
            records[field] = _record(None, field, provenance, 0, missing_reason)
        else:
            if missing_reason is not None:
                raise MetricsError(f"present field {field} cannot have missing_reason")
            records[field] = _record(value, field, provenance, 1)
    return records, series


def _series_records(
    base: str,
    values: Sequence[int | float],
    provenance: str,
    missing_reason: str | None = None,
) -> dict[str, MetricRecord]:
    if not values:
        return {
            field: _record(None, field, provenance, 0, missing_reason or MISSING_NO_SAMPLES)
            for field in SAMPLE_SERIES[base]
        }
    result: dict[str, MetricRecord] = {}
    for suffix, field in zip(PERCENTILES, SAMPLE_SERIES[base]):
        value: int | float
        if suffix == "max":
            value = max(values)
        else:
            value = percentile_nearest_rank(values, int(suffix[1:]))
        result[field] = _record(value, field, provenance, len(values))
    return result


def _derive_repeated_runs(runs: list[Mapping[str, Any]], provenance: str) -> dict[str, MetricRecord]:
    if not runs:
        return {}
    before_after = []
    run_provenances = set()
    for index, raw_run in enumerate(runs):
        run = _require_mapping(raw_run, f"repeated_runs[{index}]")
        _reject_unknown(run, _REPEATED_RUN_FIELDS, f"repeated_runs[{index}]")
        if "run_id" not in run:
            raise MetricsError(f"repeated_runs[{index}].run_id must be non-empty")
        _require_string(run["run_id"], f"repeated_runs[{index}].run_id")
        run_provenance = _require_enum(
            run.get("provenance", provenance),
            f"repeated_runs[{index}].provenance",
            PROVENANCE_VALUES,
        )
        run_provenances.add(run_provenance)
        if "memory_before_bytes" in run or "memory_after_bytes" in run:
            if "memory_before_bytes" not in run or "memory_after_bytes" not in run:
                raise MetricsError("repeated run memory snapshots must be paired")
            before = _require_int(run["memory_before_bytes"], f"repeated_runs[{index}].memory_before_bytes", maximum=UINT64_MAX)
            after = _require_int(run["memory_after_bytes"], f"repeated_runs[{index}].memory_after_bytes", maximum=UINT64_MAX)
            before_after.append((before, after))
    if len(run_provenances) > 1:
        raise MetricsError("repeated runs cannot combine mixed provenance")
    run_provenance = next(iter(run_provenances), provenance)
    result = {
        "repeated_run_count": _record(len(runs), "repeated_run_count", run_provenance, len(runs)),
    }
    if before_after:
        first_before = before_after[0][0]
        final_after = before_after[-1][1]
        drift = final_after - first_before
        result["memory_drift_bytes"] = _record(
            drift,
            "memory_drift_bytes",
            run_provenance,
            len(before_after),
        )
        result["repeated_run_memory_drift_bytes"] = _record(
            drift,
            "repeated_run_memory_drift_bytes",
            run_provenance,
            len(before_after),
        )
    cleanup_runs = [run for run in runs if "cleanup_time_us" in run]
    if cleanup_runs:
        for index, run in enumerate(cleanup_runs):
            _require_int(run["cleanup_time_us"], f"repeated cleanup_time_us[{index}]", maximum=UINT64_MAX)
        result["cleanup_time_us"] = _record(
            cleanup_runs[-1]["cleanup_time_us"],
            "cleanup_time_us",
            run_provenance,
            1,
        )
    live_runs = [run for run in runs if "live_resources_after_terminal" in run]
    if live_runs:
        result["live_resources_after_terminal"] = _record(
            _require_int(live_runs[-1]["live_resources_after_terminal"], "repeated live_resources_after_terminal", maximum=UINT64_MAX),
            "live_resources_after_terminal",
            run_provenance,
            1,
        )
    return result


def _component_audio_records(trace: Mapping[str, Any] | None) -> dict[str, MetricRecord]:
    """Validate and preserve the already-typed L10-05 model result.

    The audio model computes its own drawing-latency summaries from real model
    samples.  The aggregator does not recompute or reinterpret those records;
    it only validates their typed record shape and keeps the host provenance
    boundary intact.  New distributions supplied directly to this module must
    still use a base sample series.
    """

    if trace is None or "audio_concurrency" not in trace:
        return {}
    audio = _require_mapping(trace["audio_concurrency"], "trace.audio_concurrency")
    required_root = {
        "session_token", "scenario_id", "expected_outcome", "observed_outcome",
        "failure", "expectation_met", "provenance", "unknown_reason",
        "hardware_audio_health_measured", "resources_released",
        "cleanup_timestamps_recorded", "terminal_timestamp_us",
        "cleanup_timestamp_us", "metrics",
    }
    _reject_unknown(audio, required_root, "trace.audio_concurrency")
    missing_root = sorted(required_root - set(audio))
    if missing_root:
        raise MetricsError("audio_concurrency missing required field(s): " + ", ".join(missing_root))
    _require_int(audio["session_token"], "audio_concurrency.session_token", minimum=1, maximum=UINT64_MAX)
    _require_enum(audio["scenario_id"], "audio_concurrency.scenario_id", (
        "no-stroke-control", "sparse-drawing", "continuous-drawing", "dense-drawing",
        "rapid-color-switching", "stop-during-audio", "cancel-during-audio",
        "audio-capture-failure", "allocation-failure",
    ))
    _require_enum(audio["expected_outcome"], "audio_concurrency.expected_outcome", (
        "completed-audio-continuous", "completed-with-drawing-pressure", "stopped-and-clean",
        "cancelled-and-clean", "capture-failed-and-clean", "allocation-failed-and-clean",
    ))
    _require_enum(audio["observed_outcome"], "audio_concurrency.observed_outcome", ("completed", "stopped", "cancelled", "failed"))
    _require_enum(audio["failure"], "audio_concurrency.failure", ("none", "capture", "allocation", "invalid-configuration"))
    _require_enum(audio["provenance"], "audio_concurrency.provenance", ("host", "derived_static", "unmeasured/blocked"))
    _require_string(audio["unknown_reason"], "audio_concurrency.unknown_reason")
    _require_bool(audio["expectation_met"], "audio_concurrency.expectation_met")
    if audio["hardware_audio_health_measured"] is not False or not isinstance(audio["hardware_audio_health_measured"], bool):
        raise MetricsError("host audio model must declare hardware_audio_health_measured=false")
    _require_bool(audio["hardware_audio_health_measured"], "audio_concurrency.hardware_audio_health_measured")
    _require_bool(audio["resources_released"], "audio_concurrency.resources_released")
    _require_bool(audio["cleanup_timestamps_recorded"], "audio_concurrency.cleanup_timestamps_recorded")
    terminal_timestamp = _require_int(audio["terminal_timestamp_us"], "audio_concurrency.terminal_timestamp_us", maximum=UINT64_MAX)
    cleanup_timestamp = _require_int(audio["cleanup_timestamp_us"], "audio_concurrency.cleanup_timestamp_us", maximum=UINT64_MAX)
    if cleanup_timestamp < terminal_timestamp:
        raise MetricsError("audio cleanup timestamp precedes terminal timestamp")
    component_metrics = _require_mapping(audio["metrics"], "audio_concurrency.metrics")
    required_fields = [
        field
        for group in ("audio", "drawing_and_concurrency", "memory", "cleanup_and_repetition")
        for field in FIELD_GROUPS[group]
    ]
    missing_metrics = sorted(set(required_fields) - set(component_metrics))
    if missing_metrics:
        raise MetricsError("audio_concurrency.metrics missing required field(s): " + ", ".join(missing_metrics))
    _reject_unknown(component_metrics, METRIC_FIELDS, "audio_concurrency.metrics")
    result: dict[str, MetricRecord] = {}
    for field in required_fields:
        wrapper = _require_mapping(component_metrics[field], f"audio_concurrency.metrics.{field}")
        _reject_unknown(wrapper, {"value", "unit", "provenance", "sample_count", "missing_reason"}, f"audio_concurrency.metrics.{field}")
        if "value" not in wrapper or "provenance" not in wrapper or "sample_count" not in wrapper or "missing_reason" not in wrapper:
            raise MetricsError(f"audio_concurrency.metrics.{field} is not a complete metric record")
        _validate_unit(wrapper, field)
        provenance = wrapper["provenance"]
        _require_enum(provenance, f"audio_concurrency.metrics.{field}.provenance", PROVENANCE_VALUES)
        sample_count = _require_int(wrapper["sample_count"], f"audio_concurrency.metrics.{field}.sample_count", maximum=MAX_TOTAL_SAMPLES)
        value = _validate_value(wrapper["value"], field)
        reason = wrapper["missing_reason"]
        if reason is not None and (not isinstance(reason, str) or not reason):
            raise MetricsError(f"invalid audio metric missing_reason for {field}")
        result[field] = _record(value, field, provenance, sample_count, reason)
    return result


def _preserved_evidence(
    workload: Mapping[str, Any] | None,
    trace: Mapping[str, Any] | None,
    events: Sequence[Mapping[str, Any]] | None,
) -> dict[str, Any]:
    preserved: dict[str, Any] = {}
    if workload is not None:
        preserved["workload"] = copy.deepcopy(dict(workload))
    if trace is not None:
        preserved["trace_metadata"] = {
            key: copy.deepcopy(trace[key])
            for key in ("run_id", "firmware_build_id", "workload", "workload_metadata",
                        "evidence_kind", "clock", "bounds")
            if key in trace
        }
        if "audio_concurrency" in trace:
            audio_metadata = dict(trace["audio_concurrency"])
            audio_metadata.pop("metrics", None)
            preserved["audio_concurrency_metadata"] = copy.deepcopy(audio_metadata)
    if events is not None:
        coordinates = []
        color_changes = []
        stroke_boundaries = []
        terminal_events = []
        loss_events = []
        render_observations = []
        audio_observations = []
        for event in events:
            name = event.get("event_name")
            if name in {"touch_report", "touch_accepted", "touch_rejected", "point_coalesced"}:
                coordinates.append({
                    key: copy.deepcopy(event[key])
                    for key in ("event_id", "t_us", "provenance", "phase", "raw_touch_id",
                                "raw_x", "raw_y", "display_x", "display_y", "logical_x",
                                "logical_y", "accepted", "stroke_id", "color", "color_index",
                                "boundary")
                    if key in event
                })
            if name == "palette_changed":
                color_changes.append(copy.deepcopy(dict(event)))
            if name in {"stroke_started", "stroke_ended"} or event.get("boundary") in {
                "STROKE_START", "STROKE_END", "PEN_UP_GAP", "COLOR_CHANGE"
            }:
                stroke_boundaries.append(copy.deepcopy(dict(event)))
            if name == "terminal":
                terminal_events.append(copy.deepcopy(dict(event)))
            if name == "trace_loss":
                loss_events.append(copy.deepcopy(dict(event)))
            if name == "render_update":
                # Keep budgets and observable bounded renderer state intact;
                # they are not reducible to one high-water count.
                render_observations.append(copy.deepcopy(dict(event)))
            if name == "audio_health":
                # Occupancy and continuity must remain separate from drawing
                # queue/latency metrics and from any hardware-audio claim.
                audio_observations.append(copy.deepcopy(dict(event)))
        if len(coordinates) > MAX_PRESERVED_RECORDS:
            raise MetricsError("preserved coordinate evidence exceeds bound")
        preserved["coordinates"] = coordinates
        preserved["color_changes"] = color_changes
        preserved["stroke_and_pen_up_boundaries"] = stroke_boundaries
        preserved["terminal_events"] = terminal_events
        preserved["trace_loss_events"] = loss_events
        preserved["render_observations_with_budgets"] = render_observations
        preserved["audio_observations"] = audio_observations
    return preserved


def aggregate_observations(document: Mapping[str, Any]) -> AggregationResult:
    """Validate and aggregate one bounded observation document."""

    root = _require_mapping(document, "observation document")
    _reject_unknown(root, {"observation_schema", "provenance", "workload", "trace", "fields", "repeated_runs"}, "observation document")
    if _require_string(root.get("observation_schema"), "observation_schema") != OBSERVATION_SCHEMA:
        raise MetricsError(f"observation_schema must be {OBSERVATION_SCHEMA}")
    default_provenance = _require_enum(root.get("provenance"), "observation provenance", PROVENANCE_VALUES)

    trace = root.get("trace")
    events: list[Mapping[str, Any]] | None = None
    trace_provenance = default_provenance
    if trace is not None:
        trace = _require_mapping(trace, "trace")
        events, trace_provenance = _validate_trace(trace, default_provenance)

    workload_value = root.get("workload")
    if workload_value is None and trace is not None and "workload_metadata" in trace:
        workload_value = trace["workload_metadata"]
    workload = None
    if workload_value is not None:
        workload = _validate_workload(_require_mapping(workload_value, "workload"))

    explicit_records, explicit_series = _parse_explicit_fields(
        _require_mapping(root.get("fields", {}), "fields"), default_provenance
    )
    derived_scalars: dict[str, Any] = {}
    derived_series: dict[str, Any] = {}
    if trace is not None and events is not None:
        derived_scalars, derived_series = _trace_field_observations(trace, events, trace_provenance)
    component_records = _component_audio_records(trace)

    records: dict[str, MetricRecord] = {}
    records.update(explicit_records)
    # Explicit API observations are authoritative for a field.  Trace-derived
    # fields fill only absent fields, never silently replace them.
    for field, raw in derived_scalars.items():
        if field in records:
            continue
        value = _validate_value(raw["value"], field)
        records[field] = _record(value, field, raw["provenance"], 1)
    for base, (values, provenance, missing_reason) in explicit_series.items():
        if base in derived_series:
            raise MetricsError(f"duplicate sample source for {base}: fields and trace")
        records.update(_series_records(base, values, provenance, missing_reason))
    for base, raw in derived_series.items():
        if any(field in records for field in SAMPLE_SERIES[base]):
            continue
        records.update(_series_records(base, raw["samples"], raw["provenance"]))
    for field, record in component_records.items():
        records.setdefault(field, record)

    repeated_runs = _require_list(root.get("repeated_runs", []), "repeated_runs")
    if len(repeated_runs) > MAX_REPEATED_RUNS:
        raise MetricsError(f"repeated_runs exceeds {MAX_REPEATED_RUNS}")
    for field, record in _derive_repeated_runs(repeated_runs, default_provenance).items():
        records.setdefault(field, record)

    # Static storage dimensions are declared by the LAB-10 contract and are
    # useful even when no renderer trace is available.  They are explicitly
    # derived_static, never hardware observations.
    if "final_logical_width" not in records:
        records["final_logical_width"] = _record(120, "final_logical_width", "derived_static", 1)
    if "final_logical_height" not in records:
        records["final_logical_height"] = _record(120, "final_logical_height", "derived_static", 1)

    ordered = {field: records.get(field, _missing_record(field)) for field in METRIC_INVENTORY}
    preserved = _preserved_evidence(workload, trace, events)
    result = AggregationResult(ordered, preserved)
    try:
        output_size = len(json.dumps(result.to_dict(), ensure_ascii=False, allow_nan=False).encode("utf-8"))
    except (OverflowError, TypeError, ValueError) as exc:
        raise MetricsError(f"aggregated output is not JSON-safe: {exc}") from exc
    if output_size > MAX_OUTPUT_BYTES:
        raise MetricsError(f"aggregated output exceeds {MAX_OUTPUT_BYTES} byte limit")
    return result


def aggregate_trace(trace: Mapping[str, Any], *, provenance: str | None = None) -> AggregationResult:
    """Convenience parser for an existing trace with no extra API fields."""

    if provenance is None:
        provenance = trace.get("evidence_kind") if isinstance(trace, Mapping) else None
    if provenance not in PROVENANCE_VALUES:
        raise MetricsError("aggregate_trace requires a valid trace evidence_kind/provenance")
    workload = trace.get("workload_metadata") if isinstance(trace, Mapping) else None
    document: dict[str, Any] = {
        "observation_schema": OBSERVATION_SCHEMA,
        "provenance": provenance,
        "trace": trace,
    }
    if workload is not None:
        document["workload"] = workload
    return aggregate_observations(document)


def _write_json(path: str | Path, value: Any) -> None:
    output_path = Path(path)
    output_path.write_text(
        json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="bounded lab10-metrics-observation/v1 JSON")
    parser.add_argument("output", nargs="?", help="parsed metrics JSON; stdout when omitted")
    args = parser.parse_args(argv)
    try:
        result = aggregate_observations(load_observation_file(args.input))
        payload = json.dumps(result.to_dict(), indent=2, ensure_ascii=False) + "\n"
        if args.output:
            _write_json(args.output, result.to_dict())
        else:
            sys.stdout.write(payload)
        return 0
    except MetricsError as exc:
        sys.stderr.write(f"lab10 metrics error: {exc}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
