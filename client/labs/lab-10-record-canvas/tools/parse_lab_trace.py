#!/usr/bin/env python3
"""Parse bounded LAB-10 serial/host evidence and join L10-07 metrics.

The device emits human-readable lifecycle lines and, when instrumentation is
enabled, structured ``LAB10_EVIDENCE``/``LAB10_TRACE`` JSON.  This module owns
the L10-08 boundary only: it bounds the input, rejects malformed or ambiguous
marker/JSON data, and delegates trace/metric validation to
``aggregate_metrics``.  It never invents a hardware, audio, display, or touch
observation when evidence is absent.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Mapping, Sequence

import aggregate_metrics as aggregator


PARSER_DOCUMENT = "family-link.lab10-parser-output/v1"
PARSER_IDENTITY = "lab10-record-parser"
PARSER_VERSION = "v1"
ADAPTER_IDENTITY = "lab10"
ADAPTER_VERSION = "v1"

MAX_INPUT_BYTES = aggregator.MAX_DOCUMENT_BYTES
MAX_OUTPUT_BYTES = aggregator.MAX_OUTPUT_BYTES
MAX_LINES = aggregator.MAX_TRACE_EVENTS
MAX_LOG_LINE_BYTES = 64 * 1024
MAX_MARKER_VALUE_BYTES = 1024

_RUN_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$")
_IDENTITY = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$")
_KEY = re.compile(r"^[a-z][a-z0-9_]*$")
_MARKER_PREFIXES = (
    "LAB10_READY ",
    "LAB10_RUN ",
    "LAB10_SCENARIO ",
    "LAB10_SCRIPTED_DRAWING ",
    "LAB10_CANCEL ",
)


class ParseError(ValueError):
    """A malformed or incomplete LAB-10 evidence artifact."""


@dataclass
class MarkerState:
    ready: list[dict[str, str]] = field(default_factory=list)
    run: list[dict[str, str]] = field(default_factory=list)
    scenarios: list[dict[str, str]] = field(default_factory=list)
    scripted_drawing: list[dict[str, str]] = field(default_factory=list)
    cancel: list[dict[str, str]] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)

    def as_dict(self) -> dict[str, Any]:
        return {
            "ready": list(self.ready),
            "run": list(self.run),
            "scenarios": list(self.scenarios),
            "scripted_drawing": list(self.scripted_drawing),
            "cancel": list(self.cancel),
        }


def _duplicate_reject(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ParseError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _parse_json(payload: bytes, label: str) -> Any:
    if len(payload) > MAX_INPUT_BYTES:
        raise ParseError(f"{label} exceeds {MAX_INPUT_BYTES} bytes")

    def reject_constant(value: str) -> None:
        raise ParseError(f"{label} contains non-finite JSON number: {value}")

    try:
        return json.loads(
            payload.decode("utf-8"),
            object_pairs_hook=_duplicate_reject,
            parse_constant=reject_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ParseError(f"malformed JSON in {label}: {exc}") from exc


def _parse_marker_fields(payload: str, label: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for token in payload.split():
        if "=" not in token:
            raise ParseError(f"{label} contains a token without '='")
        key, value = token.split("=", 1)
        if not _KEY.fullmatch(key) or not value:
            raise ParseError(f"{label} contains an invalid key/value")
        if key in fields:
            raise ParseError(f"duplicate {label} field: {key}")
        if len(value.encode("utf-8")) > MAX_MARKER_VALUE_BYTES:
            raise ParseError(f"{label}.{key} exceeds marker value bound")
        fields[key] = value
    return fields


def _require_marker_fields(marker: Mapping[str, str], required: Sequence[str], label: str) -> None:
    missing = [key for key in required if key not in marker]
    if missing:
        raise ParseError(f"{label} missing required field(s): {', '.join(missing)}")


def _validate_marker(marker_name: str, fields: dict[str, str]) -> dict[str, str]:
    if marker_name == "LAB10_READY":
        _require_marker_fields(fields, ("build", "trace_schema", "physical_ring_wipe"), marker_name)
        if fields["trace_schema"] != "lab10-record-trace/v1":
            raise ParseError("LAB10_READY trace_schema is unsupported")
        if fields["physical_ring_wipe"] not in {"0", "1"}:
            raise ParseError("LAB10_READY physical_ring_wipe must be 0 or 1")
        return fields
    if marker_name == "LAB10_RUN":
        _require_marker_fields(fields, ("run_id", "mode", "workload"), marker_name)
        if not _RUN_ID.fullmatch(fields["run_id"]):
            raise ParseError("LAB10_RUN.run_id is invalid")
        if fields["mode"] not in {"manual", "scripted", "deterministic"}:
            raise ParseError("LAB10_RUN.mode must be manual, scripted, or deterministic")
        if not _IDENTITY.fullmatch(fields["workload"]):
            raise ParseError("LAB10_RUN.workload is invalid")
        return fields
    if marker_name == "LAB10_SCENARIO":
        _require_marker_fields(fields, ("id", "status"), marker_name)
        if not _IDENTITY.fullmatch(fields["id"]):
            raise ParseError("LAB10_SCENARIO.id is invalid")
        if fields["status"] not in {"started", "running", "passed", "failed", "cancelled", "not_configured"}:
            raise ParseError("LAB10_SCENARIO.status is unsupported")
        return fields
    if marker_name == "LAB10_SCRIPTED_DRAWING":
        _require_marker_fields(fields, ("status",), marker_name)
        if fields["status"] not in {"started", "complete"}:
            raise ParseError("LAB10_SCRIPTED_DRAWING.status is unsupported")
        if fields["status"] == "complete":
            required = (
                "source",
                "hardware_touch",
                "terminal",
                "duration_cap_ms",
                "accepted_points",
                "strokes",
                "palette_changes",
                "color",
            )
            _require_marker_fields(fields, required, marker_name)
            if fields["source"] != "ui_task" or fields["hardware_touch"] != "0":
                raise ParseError(
                    "LAB10_SCRIPTED_DRAWING completion must be UI-task generated "
                    "and declare hardware_touch=0"
                )
            if fields["terminal"] != "duration_cap" or fields["duration_cap_ms"] != "180000":
                raise ParseError(
                    "LAB10_SCRIPTED_DRAWING completion requires the full duration cap"
                )
            try:
                accepted_points = int(fields["accepted_points"])
            except ValueError as error:
                raise ParseError(
                    "LAB10_SCRIPTED_DRAWING.accepted_points must be an integer"
                ) from error
            if not 3500 <= accepted_points <= 4095:
                raise ParseError(
                    "LAB10_SCRIPTED_DRAWING.accepted_points is outside completion bounds"
                )
            if (
                fields["strokes"] != "1"
                or fields["palette_changes"] != "0"
                or fields["color"] != "white"
            ):
                raise ParseError(
                    "LAB10_SCRIPTED_DRAWING completion must describe one "
                    "continuous white stroke"
                )
        return fields
    if marker_name == "LAB10_CANCEL":
        _require_marker_fields(fields, ("requested",), marker_name)
        if fields["requested"] not in {"0", "1", "true", "false"}:
            raise ParseError("LAB10_CANCEL.requested must be boolean")
        return fields
    raise ParseError(f"unsupported LAB-10 marker: {marker_name}")


def _record_marker(markers: MarkerState, name: str, fields: dict[str, str]) -> None:
    if name == "LAB10_READY":
        if markers.ready:
            raise ParseError("duplicate LAB10_READY marker")
        markers.ready.append(fields)
        return
    if name == "LAB10_RUN":
        if markers.run:
            raise ParseError("duplicate LAB10_RUN marker")
        markers.run.append(fields)
        return
    if name == "LAB10_CANCEL":
        if markers.cancel:
            raise ParseError("duplicate LAB10_CANCEL marker")
        markers.cancel.append(fields)
        return
    if name == "LAB10_SCRIPTED_DRAWING":
        if fields["status"] == "complete":
            if any(item["status"] == "complete" for item in markers.scripted_drawing):
                raise ParseError("duplicate LAB10_SCRIPTED_DRAWING completion marker")
        elif any(item["status"] == "complete" for item in markers.scripted_drawing):
            raise ParseError("LAB10_SCRIPTED_DRAWING marker follows completion")
        markers.scripted_drawing.append(fields)
        return

    scenario_id = fields["id"]
    existing = [item for item in markers.scenarios if item["id"] == scenario_id]
    if existing and fields["status"] in {"passed", "failed", "cancelled", "not_configured"}:
        if any(item["status"] in {"passed", "failed", "cancelled", "not_configured"} for item in existing):
            raise ParseError(f"duplicate terminal LAB10_SCENARIO marker for {scenario_id}")
    markers.scenarios.append(fields)


def _marker_from_line(line: str) -> tuple[str, dict[str, str]] | None:
    for prefix in _MARKER_PREFIXES:
        if line.startswith(prefix):
            name = prefix[:-1]
            fields = _parse_marker_fields(line[len(prefix):], name)
            return name, _validate_marker(name, fields)
    if line == "LAB10_SCRIPTED_DRAWING" or line.startswith("LAB10_SCRIPTED_DRAWING\t"):
        raise ParseError("LAB10_SCRIPTED_DRAWING marker is malformed")
    return None


def _validate_marker_consistency(
    markers: MarkerState,
    trace: Mapping[str, Any] | None,
    *,
    expected_run_id: str | None,
    expected_mode: str | None,
) -> None:
    if expected_run_id and not _RUN_ID.fullmatch(expected_run_id):
        raise ParseError("expected_run_id is invalid")
    run = markers.run[0] if markers.run else None
    if run and expected_run_id and run["run_id"] != expected_run_id:
        raise ParseError("LAB10_RUN.run_id does not match requested run")
    if expected_mode and expected_mode not in {"manual", "scripted", "deterministic"}:
        raise ParseError("expected_mode is invalid")
    if run and expected_mode and run["mode"] != expected_mode:
        if not ({run["mode"], expected_mode} <= {"scripted", "deterministic"}):
            raise ParseError("LAB10_RUN.mode does not match requested mode")

    if trace is None:
        return
    trace_run_id = trace.get("run_id")
    if run and trace_run_id != run["run_id"]:
        raise ParseError("LAB10_RUN.run_id does not match trace.run_id")
    if expected_run_id and trace_run_id != expected_run_id:
        raise ParseError("trace.run_id does not match requested run")
    ready = markers.ready[0] if markers.ready else None
    if ready and trace.get("trace_schema") != ready["trace_schema"]:
        raise ParseError("LAB10_READY trace schema does not match trace")
    if ready and trace.get("firmware_build_id") != ready["build"]:
        raise ParseError("LAB10_READY build does not match trace firmware_build_id")
    if run:
        workload = trace.get("workload_metadata")
        identity = workload.get("identity") if isinstance(workload, Mapping) else None
        if identity != run["workload"]:
            raise ParseError("LAB10_RUN.workload does not match trace workload identity")


def _load_evidence_from_text(payload: bytes) -> tuple[dict[str, Any] | None, MarkerState]:
    if len(payload) > MAX_INPUT_BYTES:
        raise ParseError(f"serial evidence exceeds {MAX_INPUT_BYTES} bytes")
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ParseError(f"serial evidence is not UTF-8: {exc}") from exc

    lines = text.splitlines()
    if len(lines) > MAX_LINES:
        raise ParseError(f"serial evidence exceeds {MAX_LINES} lines")

    markers = MarkerState()
    roots: list[dict[str, Any]] = []
    event_rows: list[dict[str, Any]] = []
    for index, line in enumerate(lines, start=1):
        encoded_size = len(line.encode("utf-8"))
        if encoded_size > MAX_LOG_LINE_BYTES and not line.startswith("LAB10_EVIDENCE "):
            raise ParseError(f"serial line {index} exceeds {MAX_LOG_LINE_BYTES} bytes")
        if not line.strip():
            continue
        marker = _marker_from_line(line)
        if marker is not None:
            _record_marker(markers, *marker)
            continue
        if line.startswith("LAB10_INIT_FAILED"):
            markers.errors.append(line[:MAX_MARKER_VALUE_BYTES])
            continue
        if line.startswith("LAB10_EVIDENCE "):
            root = _parse_json(line[len("LAB10_EVIDENCE "):].encode("utf-8"), f"serial line {index}")
            if not isinstance(root, dict):
                raise ParseError(f"serial line {index} evidence must be an object")
            roots.append(root)
            continue
        if line.startswith("LAB10_TRACE "):
            row = _parse_json(line[len("LAB10_TRACE "):].encode("utf-8"), f"serial line {index}")
            if not isinstance(row, dict):
                raise ParseError(f"serial line {index} trace row must be an object")
            if "events" in row or "observation_schema" in row:
                roots.append(row)
            else:
                event_rows.append(row)
            continue

        # A host deterministic capture may be supplied as one plain JSON
        # document rather than wrapped in a serial marker.
        if not roots and not event_rows and line.lstrip().startswith("{"):
            root = _parse_json(line.encode("utf-8"), f"serial line {index}")
            if not isinstance(root, dict):
                raise ParseError(f"serial line {index} JSON evidence must be an object")
            roots.append(root)

    if len(roots) > 1:
        raise ParseError("multiple LAB-10 evidence documents")
    if event_rows:
        # An event stream without its required trace envelope cannot be safely
        # completed from runner markers.  In particular, do not synthesize
        # clock, bounds, build, audio, or hardware provenance.
        raise ParseError("LAB10_TRACE event rows require one complete evidence envelope")
    return (roots[0] if roots else None), markers


def _empty_aggregation(reason: str) -> dict[str, Any]:
    result = aggregator.aggregate_observations({
        "observation_schema": aggregator.OBSERVATION_SCHEMA,
        "provenance": "unmeasured/blocked",
        "fields": {},
    }).to_dict()
    result["preserved"]["missing_evidence"] = reason
    return result


def _trace_from_root(root: Mapping[str, Any]) -> tuple[dict[str, Any], dict[str, Any]]:
    if root.get("observation_schema") == aggregator.OBSERVATION_SCHEMA:
        aggregation = aggregator.aggregate_observations(root).to_dict()
        trace = root.get("trace")
        return aggregation, dict(trace) if isinstance(trace, Mapping) else {}
    if root.get("document") == "family-link.lab10-record-trace/v1":
        trace = dict(root)
        return aggregator.aggregate_trace(trace).to_dict(), trace
    raise ParseError("evidence document is neither lab10-record-trace/v1 nor lab10-metrics-observation/v1")


def _validate_experiment_bounds(trace: Mapping[str, Any]) -> None:
    """Check limits that are intentionally outside the metric aggregator's job."""

    events = trace.get("events")
    if not isinstance(events, list):
        return  # The aggregator owns the typed/schema error for this case.
    accepted = [
        event for event in events
        if isinstance(event, Mapping)
        and event.get("event_name") == "touch_accepted"
        and event.get("accepted") is True
    ]
    if len(accepted) > 4096:
        raise ParseError("trace exceeds the 4096 accepted-point cap")
    timestamps = [event.get("t_us") for event in events if isinstance(event, Mapping)]
    if timestamps and all(isinstance(value, int) and not isinstance(value, bool) for value in timestamps):
        if max(timestamps) - min(timestamps) > 180_000_000:
            raise ParseError("trace exceeds the 180000 ms duration cap")
    terminal = trace.get("terminal")
    if isinstance(terminal, Mapping) and terminal.get("outcome") == "point_cap" and len(accepted) != 4096:
        raise ParseError("point_cap terminal does not contain exactly 4096 accepted points")


def _scenario_workflow(markers: MarkerState, mode: str | None) -> tuple[bool, str | None]:
    if mode not in {"scripted", "deterministic"}:
        return True, None
    if not any(item["status"] == "complete" for item in markers.scripted_drawing):
        return False, "missing LAB10_SCRIPTED_DRAWING status=complete marker"
    terminal = [item for item in markers.scenarios if item["status"] in {"passed", "failed", "cancelled", "not_configured"}]
    if any(item["status"] == "failed" for item in terminal):
        return False, "LAB10_SCENARIO reported failed"
    if any(item["status"] == "cancelled" for item in terminal):
        return False, "LAB10_SCENARIO was cancelled"
    # The completion marker is device-owned terminal evidence. A current
    # adapter appends status=passed; an older status=not_configured fallback
    # (or no host terminal) is accepted only because the exact completion
    # marker was observed above.
    return True, None


def parse_bytes(
    payload: bytes,
    *,
    expected_run_id: str | None = None,
    expected_mode: str | None = None,
) -> dict[str, Any]:
    """Return bounded structured parser output for one serial/host artifact."""

    root, markers = _load_evidence_from_text(payload)
    trace: dict[str, Any] | None = None
    if root is not None:
        if root.get("document") == "family-link.lab10-record-trace/v1":
            trace = root
        elif isinstance(root.get("trace"), Mapping):
            trace = dict(root["trace"])
    _validate_marker_consistency(
        markers,
        trace,
        expected_run_id=expected_run_id,
        expected_mode=expected_mode,
    )

    marker_mode = markers.run[0]["mode"] if markers.run else expected_mode
    if marker_mode == "deterministic":
        marker_mode = "scripted"
    scenario_ok, scenario_reason = _scenario_workflow(markers, marker_mode)
    marker_errors = list(markers.errors)
    if not markers.ready:
        marker_errors.append("missing LAB10_READY marker")
    if not markers.run:
        marker_errors.append("missing LAB10_RUN marker")
    if scenario_reason:
        marker_errors.append(scenario_reason)

    if root is None:
        aggregation = _empty_aggregation("no LAB10_EVIDENCE or complete host trace was supplied")
        evidence_provenance = "unmeasured/blocked"
        trace_outcome = None
    else:
        if trace is not None:
            _validate_experiment_bounds(trace)
        aggregation, trace_value = _trace_from_root(root)
        trace = trace or (trace_value if trace_value else None)
        evidence_provenance = (
            trace.get("evidence_kind") if trace else root.get("provenance")
        )
        trace_outcome = trace.get("terminal", {}).get("outcome") if trace else None

    if not isinstance(evidence_provenance, str):
        evidence_provenance = "unmeasured/blocked"

    cancel_requested = bool(markers.cancel and markers.cancel[0].get("requested") in {"1", "true"})
    if cancel_requested and trace_outcome is None:
        marker_errors.append("LAB10_CANCEL has no terminal trace evidence")
    if cancel_requested and trace_outcome not in {None, "cancelled"}:
        marker_errors.append("LAB10_CANCEL conflicts with trace terminal outcome")
    workflow_status = "PASS" if not marker_errors else "INCOMPLETE"
    if markers.errors:
        workflow_status = "FAIL"

    # A completed trace is still not an experiment PASS: L10-08 cannot infer
    # hardware visual, touch, audio, scanout, or task/resource success.
    if trace_outcome == "failed":
        experiment_verdict = "FAIL"
    elif trace_outcome in {"stopped", "cancelled"}:
        experiment_verdict = "INCOMPLETE"
    else:
        experiment_verdict = "UNMEASURED"

    run_id = expected_run_id or (markers.run[0]["run_id"] if markers.run else None)
    if trace:
        run_id = trace["run_id"]
    output: dict[str, Any] = {
        "document": PARSER_DOCUMENT,
        "parser": {
            "identity": PARSER_IDENTITY,
            "version": PARSER_VERSION,
            "adapter_identity": ADAPTER_IDENTITY,
            "adapter_version": ADAPTER_VERSION,
            "input_limit_bytes": MAX_INPUT_BYTES,
            "output_limit_bytes": MAX_OUTPUT_BYTES,
        },
        "run_id": run_id,
        "workflow_status": workflow_status,
        "experiment_verdict": experiment_verdict,
        "evidence_provenance": evidence_provenance,
        "markers": markers.as_dict(),
        "diagnostics": {
            "errors": marker_errors,
            "trace_present": trace is not None,
            "trace_outcome": trace_outcome,
            "cancel_requested": cancel_requested,
            "scripted_completion_observed": any(
                item["status"] == "complete" for item in markers.scripted_drawing
            ),
        },
        **aggregation,
    }
    encoded = json.dumps(output, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")
    if len(encoded) > MAX_OUTPUT_BYTES:
        raise ParseError(f"parser output exceeds {MAX_OUTPUT_BYTES} bytes")
    return output


def parse_file(
    path: str | Path,
    *,
    output_path: str | Path | None = None,
    expected_run_id: str | None = None,
    expected_mode: str | None = None,
) -> dict[str, Any]:
    input_path = Path(path)
    try:
        if input_path.stat().st_size > MAX_INPUT_BYTES:
            raise ParseError(f"serial evidence exceeds {MAX_INPUT_BYTES} bytes")
        payload = input_path.read_bytes()
    except OSError as exc:
        raise ParseError(f"cannot read serial evidence: {exc}") from exc
    result = parse_bytes(payload, expected_run_id=expected_run_id, expected_mode=expected_mode)
    if output_path is not None:
        destination = Path(output_path)
        encoded = json.dumps(result, indent=2, ensure_ascii=False, allow_nan=False) + "\n"
        if len(encoded.encode("utf-8")) > MAX_OUTPUT_BYTES:
            raise ParseError(f"parser output exceeds {MAX_OUTPUT_BYTES} bytes")
        destination.parent.mkdir(parents=True, exist_ok=True)
        temporary = destination.with_name(destination.name + f".{os.getpid()}.tmp")
        try:
            temporary.write_text(encoded, encoding="utf-8")
            os.replace(temporary, destination)
        finally:
            temporary.unlink(missing_ok=True)
    return result


def parse(path: str | Path, run_id: str | None = None, *, mode: str | None = None) -> dict[str, Any]:
    """Compatibility entry point used by host tests and small lab scripts."""

    return parse_file(path, expected_run_id=run_id, expected_mode=mode)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="bounded LAB-10 serial.txt or host trace")
    parser.add_argument("-o", "--output", help="write parser output JSON")
    parser.add_argument("--run-id", default=None)
    parser.add_argument("--mode", choices=("manual", "scripted", "deterministic"), default=None)
    args = parser.parse_args(argv)
    try:
        result = parse_file(args.input, output_path=args.output,
                            expected_run_id=args.run_id, expected_mode=args.mode)
        if args.output is None:
            sys.stdout.write(json.dumps(result, indent=2, ensure_ascii=False) + "\n")
        return 0
    except (ParseError, aggregator.MetricsError) as exc:
        sys.stderr.write(f"lab10 parser error: {exc}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
