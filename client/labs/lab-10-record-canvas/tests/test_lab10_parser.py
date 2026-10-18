#!/usr/bin/env python3
"""Host regressions for the L10-08 parser/runner evidence boundary."""

from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path


PACKAGE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE / "tools"))

import aggregate_metrics as aggregator  # noqa: E402
import parse_lab_trace as parser  # noqa: E402


def _event(event_id: str, name: str, timestamp: int, phase: str = "NONE", **extra):
    return {
        "event_id": event_id,
        "event_name": name,
        "t_us": timestamp,
        "provenance": "host",
        "phase": phase,
        **extra,
    }


def trace_fixture(outcome: str = "completed") -> dict:
    events = [
        _event("e-1", "capture_started", 0),
        _event(
            "e-2", "touch_report", 10, "DOWN", raw_touch_id=0,
            raw_x=10, raw_y=10, display_x=10, display_y=10,
            accepted=False, stroke_id=1, color="#F8F8F8",
        ),
        _event(
            "e-3", "touch_accepted", 20, "DOWN", raw_touch_id=0,
            raw_x=10, raw_y=10, display_x=10, display_y=10,
            logical_x=5, logical_y=5, accepted=True, stroke_id=1,
            color="#F8F8F8", color_index=1, boundary="STROKE_START",
        ),
        _event("e-4", "stroke_ended", 30, "UP", stroke_id=1),
        _event("e-5", "terminal", 40, "TERMINAL", outcome=outcome, terminal_event_count=1),
    ]
    return {
        "document": "family-link.lab10-record-trace/v1",
        "trace_schema": "lab10-record-trace/v1",
        "run_id": "parser-run-1",
        "lab_id": "LAB-10",
        "lab_revision": "v1",
        "firmware_build_id": "parser-build",
        "session_generation": 1,
        "workload": "synthetic-human-like-v1",
        "workload_metadata": {
            "identity": "synthetic-sparse-multicolor-v1",
            "schema_version": "lab10-workload/v1",
            "source": "synthetic",
            "evidence_provenance": "host",
            "seed": 7,
            "generator_version": "synthetic-human-like-v1",
            "timestamp_jitter_bound_us": 2000,
            "declared_features": ["variable_spacing"],
        },
        "evidence_kind": "host",
        "clock": {"source": "monotonic_host", "unit": "us", "timestamp_jitter_bound_us": 2000},
        "bounds": {
            "lcd_width_px": 320, "lcd_height_px": 240,
            "canvas_width_px": 240, "canvas_height_px": 240,
            "rail_origin_x_px": 240, "logical_width": 120,
            "logical_height": 120, "duration_cap_ms": 180000,
            "accepted_point_cap": 4096, "queue_capacity": 16,
            "minimum_move_distance_px": 2,
        },
        "events": events,
        "terminal": {"event_id": "e-5", "outcome": outcome, "event_count": 1},
    }


def serial_for(
    trace=None,
    *,
    mode="scripted",
    run_id="parser-run-1",
    scenario_status=None,
    scripted_marker=None,
    extra="",
):
    workload = "synthetic-sparse-multicolor-v1" if mode == "scripted" else "manual-no-stroke-audio-control"
    if scenario_status is None:
        scenario_status = "passed" if mode == "scripted" else "not_configured"
    lines = [
        f"LAB10_RUN run_id={run_id} mode={mode} workload={workload} adapter_version=v1",
        "LAB10_READY build=parser-build trace_schema=lab10-record-trace/v1 physical_ring_wipe=1",
        f"LAB10_SCENARIO id={workload} status=started",
    ]
    if mode == "scripted":
        lines.extend([
            scripted_marker
            if scripted_marker is not None
            else "LAB10_SCRIPTED_DRAWING status=complete source=ui_task hardware_touch=0 recording=continues",
        ])
    lines.append(f"LAB10_SCENARIO id={workload} status={scenario_status}")
    if extra:
        lines.append(extra)
    if trace is not None:
        lines.append("LAB10_EVIDENCE " + json.dumps(trace, separators=(",", ":")))
    return ("\n".join(lines) + "\n").encode()


class ParserMalformedAndMissingTests(unittest.TestCase):
    def test_missing_trace_is_explicit_unmeasured_but_workflow_can_pass(self):
        result = parser.parse_bytes(serial_for())
        self.assertEqual(result["workflow_status"], "PASS")
        self.assertEqual(result["experiment_verdict"], "UNMEASURED")
        self.assertFalse(result["diagnostics"]["trace_present"])
        self.assertEqual(result["metrics"]["accepted_point_count"]["value"], None)
        self.assertTrue(result["metrics"]["accepted_point_count"]["missing_reason"])

    def test_malformed_json_missing_fields_wrong_types_and_unknown_fields_fail_closed(self):
        base = serial_for()
        with self.assertRaises(parser.ParseError):
            parser.parse_bytes(base + b"LAB10_EVIDENCE {not-json}\n")

        missing = trace_fixture()
        del missing["terminal"]
        with self.assertRaises(aggregator.MetricsError):
            parser.parse_bytes(serial_for(missing))

        wrong_type = trace_fixture()
        wrong_type["events"][2]["color"] = 3
        with self.assertRaises(aggregator.MetricsError):
            parser.parse_bytes(serial_for(wrong_type))

        unknown = trace_fixture()
        unknown["events"][2]["unexpected"] = True
        with self.assertRaises(aggregator.MetricsError):
            parser.parse_bytes(serial_for(unknown))

    def test_duplicate_terminal_and_post_terminal_are_rejected(self):
        duplicate = trace_fixture()
        duplicate["events"].insert(-1, copy.deepcopy(duplicate["events"][-1]))
        duplicate["events"][-2]["event_id"] = "e-5b"
        with self.assertRaises(aggregator.MetricsError):
            parser.parse_bytes(serial_for(duplicate))

        after = trace_fixture()
        after["events"].append(_event("e-6", "capture_started", 41))
        with self.assertRaises(aggregator.MetricsError):
            parser.parse_bytes(serial_for(after))


class ParserLifecycleAndBoundTests(unittest.TestCase):
    def test_marker_uniqueness_readiness_run_scenario_and_mode_separation(self):
        duplicate_ready = serial_for() + b"LAB10_READY build=parser-build trace_schema=lab10-record-trace/v1 physical_ring_wipe=1\n"
        with self.assertRaises(parser.ParseError):
            parser.parse_bytes(duplicate_ready)

        missing_ready = serial_for().replace(b"LAB10_READY build=parser-build trace_schema=lab10-record-trace/v1 physical_ring_wipe=1\n", b"")
        self.assertEqual(parser.parse_bytes(missing_ready)["workflow_status"], "INCOMPLETE")

        duplicate_scenario = serial_for() + b"LAB10_SCENARIO id=synthetic-sparse-multicolor-v1 status=passed\n"
        with self.assertRaises(parser.ParseError):
            parser.parse_bytes(duplicate_scenario)

        manual = parser.parse_bytes(serial_for(mode="manual", trace=None))
        self.assertEqual(manual["markers"]["run"][0]["mode"], "manual")
        self.assertEqual(manual["workflow_status"], "PASS")
        self.assertEqual(manual["experiment_verdict"], "UNMEASURED")
        self.assertEqual(manual["markers"]["scenarios"][-1]["status"], "not_configured")
        self.assertEqual(manual["markers"]["scripted_drawing"], [])
        scripted = parser.parse_bytes(serial_for(mode="scripted", trace=trace_fixture()))
        self.assertEqual(scripted["markers"]["run"][0]["mode"], "scripted")
        self.assertEqual(scripted["workflow_status"], "PASS")
        self.assertEqual(scripted["markers"]["scripted_drawing"][-1]["status"], "complete")

    def test_scripted_completion_is_required_and_malformed_marker_fails_closed(self):
        missing = parser.parse_bytes(serial_for(mode="scripted", scripted_marker=""))
        self.assertEqual(missing["workflow_status"], "INCOMPLETE")
        self.assertIn("missing LAB10_SCRIPTED_DRAWING status=complete marker",
                      missing["diagnostics"]["errors"])

        malformed = serial_for(
            mode="scripted", scripted_marker="LAB10_SCRIPTED_DRAWING status=finished"
        )
        with self.assertRaises(parser.ParseError):
            parser.parse_bytes(malformed)

        passed_without_device_completion = serial_for(
            mode="scripted", scripted_marker="", scenario_status="passed"
        )
        result = parser.parse_bytes(passed_without_device_completion)
        self.assertEqual(result["workflow_status"], "INCOMPLETE")
        self.assertEqual(result["experiment_verdict"], "UNMEASURED")

    def test_cancellation_is_preserved_and_not_promoted_to_pass(self):
        trace = trace_fixture("cancelled")
        payload = serial_for(trace) + b"LAB10_CANCEL requested=1 reason=operator\n"
        result = parser.parse_bytes(payload)
        self.assertEqual(result["experiment_verdict"], "INCOMPLETE")
        self.assertTrue(result["diagnostics"]["cancel_requested"])
        self.assertEqual(len(result["markers"]["cancel"]), 1)

        no_terminal = serial_for() + b"LAB10_CANCEL requested=1 reason=operator\n"
        self.assertEqual(parser.parse_bytes(no_terminal)["workflow_status"], "INCOMPLETE")

    def test_colors_strokes_and_capacity_bounds_fail_closed(self):
        bad_color = trace_fixture()
        bad_color["events"][2]["color"] = "#FFFF"
        with self.assertRaises(aggregator.MetricsError):
            parser.parse_bytes(serial_for(bad_color))

        bad_stroke = trace_fixture()
        bad_stroke["events"][2]["stroke_id"] = 0
        with self.assertRaises(aggregator.MetricsError):
            parser.parse_bytes(serial_for(bad_stroke))

        over_cap = trace_fixture()
        accepted = over_cap["events"][2]
        for index in range(4096):
            row = copy.deepcopy(accepted)
            row["event_id"] = f"e-a{index}"
            row["t_us"] = 50 + index
            over_cap["events"].insert(-1, row)
        with self.assertRaises(parser.ParseError):
            parser.parse_bytes(serial_for(over_cap))

        over_duration = trace_fixture()
        over_duration["events"][-1]["t_us"] = 180_000_001
        with self.assertRaises(parser.ParseError):
            parser.parse_bytes(serial_for(over_duration))

    def test_bounded_input_and_output_file_creation(self):
        with self.assertRaises(parser.ParseError):
            parser.parse_bytes(b"x" * (parser.MAX_INPUT_BYTES + 1))

        with tempfile.TemporaryDirectory() as td:
            input_path = Path(td) / "serial.txt"
            output_path = Path(td) / "metrics.json"
            input_path.write_bytes(serial_for())
            result = parser.parse_file(input_path, output_path=output_path)
            self.assertTrue(output_path.is_file())
            self.assertEqual(json.loads(output_path.read_text())["document"], parser.PARSER_DOCUMENT)
            self.assertLessEqual(output_path.stat().st_size, parser.MAX_OUTPUT_BYTES)
            self.assertEqual(result["workflow_status"], "PASS")


class VerdictAndParityTests(unittest.TestCase):
    def test_workflow_and_experiment_verdicts_are_independent(self):
        result = parser.parse_bytes(serial_for())
        self.assertEqual((result["workflow_status"], result["experiment_verdict"]), ("PASS", "UNMEASURED"))
        trace_result = parser.parse_bytes(serial_for(trace_fixture()))
        self.assertEqual((trace_result["workflow_status"], trace_result["experiment_verdict"]), ("PASS", "UNMEASURED"))

    def test_parser_reuses_exact_aggregator_metric_and_unit_contract(self):
        result = parser.parse_bytes(serial_for(trace_fixture()))
        manifest = json.loads((PACKAGE / "manifest.lab.json").read_text())
        inventory = [field for group in manifest["metric_schema"]["fields"].values() for field in group]
        self.assertEqual(list(result["metrics"]), inventory)
        self.assertEqual(set(result["metrics"]), set(aggregator.METRIC_INVENTORY))
        self.assertEqual(result["metric_schema"], "manifest.lab.json#/metric_schema")
        self.assertEqual(result["parser"]["adapter_identity"], "lab10")


if __name__ == "__main__":
    unittest.main()
