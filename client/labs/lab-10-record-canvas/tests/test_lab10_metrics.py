#!/usr/bin/env python3
"""Strict host tests for the L10-07 bounded observation aggregator."""

import copy
import sys
import unittest
from pathlib import Path


PACKAGE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE / "tools"))

import aggregate_metrics as metrics  # noqa: E402


def observation(fields=None, *, provenance="host", trace=None, runs=None, workload=None):
    document = {
        "observation_schema": metrics.OBSERVATION_SCHEMA,
        "provenance": provenance,
        "fields": fields or {},
    }
    if trace is not None:
        document["trace"] = trace
    if runs is not None:
        document["repeated_runs"] = runs
    if workload is not None:
        document["workload"] = workload
    return document


def trace_fixture():
    def event(event_id, name, timestamp, phase="NONE", **extra):
        return {
            "event_id": event_id,
            "event_name": name,
            "t_us": timestamp,
            "provenance": "host",
            "phase": phase,
            **extra,
        }

    touch_common = {
        "raw_touch_id": 0,
        "raw_x": 0,
        "raw_y": 239,
        "display_x": 0,
        "display_y": 239,
        "stroke_id": 1,
        "color": "#F8F8F8",
        "accepted": False,
    }
    events = [
        event("e-1", "capture_started", 0),
        event("e-2", "touch_report", 10, "DOWN", **touch_common),
        event(
            "e-3", "touch_accepted", 20, "DOWN", **{
                **touch_common,
                "accepted": True,
                "logical_x": 0,
                "logical_y": 119,
                "color_index": 1,
                "boundary": "STROKE_START",
            }
        ),
        event(
            "e-4", "point_coalesced", 25, "MOVE", **{
                **touch_common,
                "reason": "coalesced",
            }
        ),
        event(
            "e-5", "touch_rejected", 26, "MOVE", **{
                **touch_common,
                "accepted": False,
                "reason": "second_touch",
            }
        ),
        event(
            "e-6", "palette_changed", 30, "PALETTE_CHANGE",
            selected_color="#E53935",
        ),
        event("e-7", "stroke_started", 31, "DOWN", stroke_id=2, color="#E53935"),
        event(
            "e-8", "ui_update", 40, duration_us=9, update_kind="ink", invalidation_count=1
        ),
        event(
            "e-9", "render_update", 50,
            render_mode="direct_point",
            render_role="control",
            frame_phase="idle",
            lock_state="ui_task_owned",
            points_processed=1,
            raster_ops=1,
            publication_ops=1,
            backlog_points=2,
            raster_budget=32,
            publication_budget=32,
            point_budget=1,
            frame_generation=1,
            invalidation_count=1,
            full_screen_invalidation=False,
            presentation_pending=True,
        ),
        event(
            "e-10", "audio_health", 60,
            audio_continuity_status="continuous",
            audio_occupancy_bytes=256,
            audio_high_water_bytes=512,
        ),
        event(
            "e-11", "cleanup", 70,
            cleanup_kind="stop",
            duration_us=7,
            live_resources=0,
        ),
        event(
            "e-12", "terminal", 80, "TERMINAL",
            outcome="completed",
            terminal_event_count=1,
        ),
    ]
    return {
        "document": "family-link.lab10-record-trace/v1",
        "trace_schema": "lab10-record-trace/v1",
        "run_id": "host-test-1",
        "lab_id": "LAB-10",
        "lab_revision": "v1",
        "firmware_build_id": "host-test",
        "session_generation": 1,
        "workload": "synthetic-human-like-v1",
        "workload_metadata": {
            "identity": "synthetic-sparse-multicolor-v1",
            "schema_version": "lab10-workload/v1",
            "source": "synthetic",
            "evidence_provenance": "host",
            "seed": 1,
            "generator_version": "synthetic-human-like-v1",
            "timestamp_jitter_bound_us": 2000,
            "declared_features": ["variable_spacing"],
        },
        "evidence_kind": "host",
        "clock": {
            "source": "monotonic_host",
            "unit": "us",
            "timestamp_jitter_bound_us": 2000,
        },
        "bounds": {
            "lcd_width_px": 320,
            "lcd_height_px": 240,
            "canvas_width_px": 240,
            "canvas_height_px": 240,
            "rail_origin_x_px": 240,
            "logical_width": 120,
            "logical_height": 120,
            "duration_cap_ms": 180000,
            "accepted_point_cap": 4096,
            "queue_capacity": 16,
            "minimum_move_distance_px": 2,
        },
        "events": events,
        "terminal": {"event_id": "e-12", "outcome": "completed", "event_count": 1},
    }


class PercentileTest(unittest.TestCase):
    def test_nearest_rank_is_deterministic(self):
        values = [4, 1, 3, 2]
        self.assertEqual(metrics.percentile_nearest_rank(values, 50), 2)
        self.assertEqual(metrics.percentile_nearest_rank(values, 95), 4)
        self.assertEqual(metrics.percentile_nearest_rank(values, 99), 4)
        with self.assertRaises(metrics.MetricsError):
            metrics.percentile_nearest_rank([float("nan")], 50)

    def test_empty_series_is_missing_not_zero(self):
        result = metrics.aggregate_observations(
            observation({"ui_update_us": {"samples": [], "unit": "us"}})
        )
        record = result.metrics["ui_update_us_p50"]
        self.assertIsNone(record.value)
        self.assertEqual(record.sample_count, 0)
        self.assertEqual(record.provenance, "host")
        self.assertTrue(record.missing_reason)

    def test_percentile_input_alias_is_not_silently_accepted(self):
        with self.assertRaises(metrics.MetricsError):
            metrics.aggregate_observations(
                observation({"ui_update_us_p50": {"value": 1, "unit": "us"}})
            )


class RecordAndProvenanceTest(unittest.TestCase):
    def test_output_order_units_and_hardware_missing_reasons(self):
        result = metrics.aggregate_observations(observation())
        self.assertEqual(list(result.metrics), metrics.METRIC_INVENTORY)
        record = result.metrics["capture_worker_stack_high_water_bytes"]
        self.assertEqual(record.unit, "bytes")
        self.assertEqual(record.provenance, "unmeasured/blocked")
        self.assertIn("hardware-only", record.missing_reason)
        self.assertEqual(result.metrics["final_logical_width"].value, 120)
        self.assertEqual(result.metrics["final_logical_width"].provenance, "derived_static")

    def test_field_provenance_and_signed_memory_drift_are_preserved(self):
        result = metrics.aggregate_observations(
            observation(
                {
                    "raw_report_count": {
                        "value": 7,
                        "unit": "count",
                        "provenance": "hardware",
                    },
                    "memory_drift_bytes": {
                        "value": -8,
                        "unit": "bytes",
                        "provenance": "host",
                    },
                    "final_format_version": {
                        "value": "indexed-six-color-v1",
                        "unit": "version",
                        "provenance": "derived_static",
                    },
                }
            )
        )
        self.assertEqual(result.metrics["raw_report_count"].provenance, "hardware")
        self.assertEqual(result.metrics["memory_drift_bytes"].value, -8)
        self.assertEqual(result.metrics["final_format_version"].unit, "version")


class TraceAggregationTest(unittest.TestCase):
    def test_trace_derives_queue_audio_render_cleanup_and_preserves_coordinates(self):
        result = metrics.aggregate_trace(trace_fixture())
        self.assertEqual(result.metrics["raw_report_count"].value, 1)
        self.assertEqual(result.metrics["accepted_point_count"].value, 1)
        self.assertEqual(result.metrics["coalesced_move_count"].value, 1)
        self.assertEqual(result.metrics["rejected_second_touch_count"].value, 1)
        self.assertEqual(result.metrics["edge_point_count"].value, 1)
        self.assertEqual(result.metrics["terminal_event_count"].value, 1)
        self.assertEqual(result.metrics["terminal_outcome"].value, "completed")
        self.assertEqual(result.metrics["ui_update_us_p50"].value, 9)
        self.assertEqual(result.metrics["render_mode"].value, "direct_point")
        self.assertEqual(result.metrics["audio_buffer_high_water_bytes"].value, 512)
        self.assertEqual(result.metrics["cleanup_time_us"].value, 7)
        self.assertEqual(result.metrics["stop_cleanup_us_p50"].value, 7)
        self.assertEqual(result.metrics["live_resources_after_terminal"].value, 0)
        self.assertEqual(result.metrics["queue_overflow_count"].value, 0)
        self.assertEqual(result.preserved["coordinates"][0]["raw_x"], 0)
        self.assertEqual(result.preserved["coordinates"][1]["logical_y"], 119)
        self.assertEqual(len(result.preserved["color_changes"]), 1)
        self.assertEqual(len(result.preserved["stroke_and_pen_up_boundaries"]), 2)
        self.assertEqual(result.preserved["render_observations_with_budgets"][0]["raster_budget"], 32)
        self.assertEqual(result.preserved["audio_observations"][0]["audio_continuity_status"], "continuous")
        self.assertEqual(result.preserved["terminal_events"][0]["outcome"], "completed")

    def test_trace_loss_and_cap_outcome_are_explicit(self):
        trace = trace_fixture()
        trace["events"].insert(
            -1,
            {
                "event_id": "e-13",
                "event_name": "trace_loss",
                "t_us": 75,
                "provenance": "host",
                "phase": "NONE",
                "reason": "queue_overflow",
                "lost_event_count": 3,
            },
        )
        trace["events"][-1]["outcome"] = "queue_overflow"
        trace["terminal"]["outcome"] = "queue_overflow"
        result = metrics.aggregate_trace(trace)
        self.assertEqual(result.metrics["queue_overflow_count"].value, 1)
        self.assertEqual(result.metrics["trace_loss_count"].value, 3)
        self.assertEqual(result.metrics["point_cap_hit_count"].value, 0)

    def test_typed_audio_concurrency_records_are_validated_and_preserved(self):
        trace = trace_fixture()
        required = [
            field
            for group in ("audio", "drawing_and_concurrency", "memory", "cleanup_and_repetition")
            for field in metrics.FIELD_GROUPS[group]
        ]
        typed = {}
        for field in required:
            unit = metrics.FIELD_UNITS[field]
            if field in metrics.HARDWARE_ONLY_FIELDS:
                value = None
                missing_reason = "hardware-only metric unavailable in host model"
                sample_count = 0
            elif unit == "enum":
                value = "model-continuous" if field == "audio_continuity_status" else "completed"
                missing_reason = None
                sample_count = 1
            elif unit == "boolean":
                value = True
                missing_reason = None
                sample_count = 1
            elif field in {"memory_drift_bytes", "repeated_run_memory_drift_bytes"}:
                value = -1
                missing_reason = None
                sample_count = 1
            else:
                value = 1
                missing_reason = None
                sample_count = 1
            typed[field] = {
                "value": value,
                "unit": unit,
                "provenance": "host",
                "sample_count": sample_count,
                "missing_reason": missing_reason,
            }
        trace["audio_concurrency"] = {
            "session_token": 1,
            "scenario_id": "dense-drawing",
            "expected_outcome": "completed-with-drawing-pressure",
            "observed_outcome": "completed",
            "failure": "none",
            "expectation_met": True,
            "provenance": "host",
            "unknown_reason": "host bounded model only",
            "hardware_audio_health_measured": False,
            "resources_released": True,
            "cleanup_timestamps_recorded": True,
            "terminal_timestamp_us": 80,
            "cleanup_timestamp_us": 87,
            "metrics": typed,
        }
        result = metrics.aggregate_trace(trace)
        self.assertEqual(result.metrics["audio_frame_count_produced"].value, 1)
        self.assertEqual(result.metrics["memory_drift_bytes"].value, -1)
        self.assertIn("audio_concurrency_metadata", result.preserved)
        self.assertNotIn("metrics", result.preserved["audio_concurrency_metadata"])


class RepetitionAndBoundTest(unittest.TestCase):
    def test_repeated_run_count_and_signed_drift(self):
        result = metrics.aggregate_observations(
            observation(
                runs=[
                    {"run_id": "r1", "memory_before_bytes": 100, "memory_after_bytes": 103, "cleanup_time_us": 4, "live_resources_after_terminal": 0},
                    {"run_id": "r2", "memory_before_bytes": 104, "memory_after_bytes": 110, "cleanup_time_us": 7, "live_resources_after_terminal": 0},
                ]
            )
        )
        self.assertEqual(result.metrics["repeated_run_count"].value, 2)
        self.assertEqual(result.metrics["memory_drift_bytes"].value, 10)
        self.assertEqual(result.metrics["repeated_run_memory_drift_bytes"].value, 10)
        self.assertEqual(result.metrics["repeated_run_memory_drift_bytes"].sample_count, 2)
        self.assertEqual(result.metrics["cleanup_time_us"].value, 7)

    def test_unknown_malformed_and_oversized_input_fails_closed(self):
        bad = observation({"not_a_metric": {"value": 1, "unit": "count"}})
        with self.assertRaises(metrics.MetricsError):
            metrics.aggregate_observations(bad)
        bad = observation({"raw_report_count": {"value": -1, "unit": "count"}})
        with self.assertRaises(metrics.MetricsError):
            metrics.aggregate_observations(bad)
        bad = observation({"ui_update_us": {"samples": [1] * (metrics.MAX_SAMPLES_PER_SERIES + 1)}})
        with self.assertRaises(metrics.MetricsError):
            metrics.aggregate_observations(bad)
        with self.assertRaises(metrics.MetricsError):
            metrics.load_json_bytes(b'{"x": 1, "x": 2}')
        with self.assertRaises(metrics.MetricsError):
            metrics.load_json_bytes(b"{" + b"a" * (metrics.MAX_DOCUMENT_BYTES + 1) + b"}")

    def test_manifest_unit_and_field_parity(self):
        self.assertEqual(set(metrics.FIELD_UNITS), set(metrics.METRIC_INVENTORY))
        self.assertEqual(
            set(field for fields in metrics.FIELD_GROUPS.values() for field in fields),
            set(metrics.FIELD_UNITS),
        )
        for base, outputs in metrics.SAMPLE_SERIES.items():
            self.assertEqual(outputs, tuple(f"{base}_{suffix}" for suffix in metrics.PERCENTILES))


class WorkloadIdentityTest(unittest.TestCase):
    def test_workload_identity_and_provenance_are_preserved(self):
        workload = {
            "identity": "synthetic-sparse-multicolor-v1",
            "schema_version": "lab10-workload/v1",
            "source": "synthetic",
            "evidence_provenance": "host",
            "seed": 9,
            "generator_version": "synthetic-human-like-v1",
            "timestamp_jitter_bound_us": 2000,
            "declared_features": ["variable_spacing", "color_changes"],
        }
        result = metrics.aggregate_observations(observation(workload=workload))
        self.assertEqual(result.preserved["workload"], workload)


class TraceValidationRegressionTest(unittest.TestCase):
    def assert_trace_rejected(self, mutate):
        trace = trace_fixture()
        mutate(trace)
        with self.assertRaises(metrics.MetricsError):
            metrics.aggregate_trace(trace)

    def test_required_identity_strings_and_session_generation_fail_closed(self):
        for field in ("document", "trace_schema", "run_id", "lab_revision", "firmware_build_id", "lab_id"):
            self.assert_trace_rejected(lambda trace, field=field: trace.__setitem__(field, ""))
        self.assert_trace_rejected(lambda trace: trace["workload_metadata"].__setitem__("identity", ""))
        self.assert_trace_rejected(lambda trace: trace["workload_metadata"].__setitem__("schema_version", ""))
        self.assert_trace_rejected(lambda trace: trace.__setitem__("session_generation", 0))
        self.assert_trace_rejected(lambda trace: trace.__setitem__("session_generation", 2**64 - 1))
        self.assert_trace_rejected(lambda trace: trace.__setitem__("session_generation", "1"))

    def test_event_enums_conditional_values_and_ranges_fail_closed(self):
        probes = [
            lambda trace: trace["events"][0].__setitem__("phase", "MOVE"),
            lambda trace: trace["events"][1].__setitem__("event_name", "unknown"),
            lambda trace: trace["events"][2].__setitem__("accepted", False),
            lambda trace: trace["events"][3].__setitem__("reason", "not-coalesced"),
            lambda trace: trace["events"][1].__setitem__("raw_touch_id", 256),
            lambda trace: trace["events"][1].__setitem__("raw_x", 32768),
            lambda trace: trace["events"][2].__setitem__("logical_x", 120),
            lambda trace: trace["events"][2].__setitem__("stroke_id", 0),
            lambda trace: trace["events"][1].__setitem__("t_us", -1),
            lambda trace: trace["events"][1].__setitem__("ideal_t_us", "10"),
            lambda trace: trace["events"][1].__setitem__("timestamp_jitter_us", 2001),
            lambda trace: trace["events"][5].__setitem__("selected_color", "red"),
            lambda trace: trace["events"][7].__setitem__("update_kind", "draw"),
            lambda trace: trace["events"][8].__setitem__("render_mode", "bogus"),
            lambda trace: trace["events"][8].__setitem__("raster_budget", 0),
            lambda trace: trace["events"][8].__setitem__("point_budget", 65),
            lambda trace: trace["events"][8].__setitem__("presentation_pending", "false"),
            lambda trace: trace["events"][9].__setitem__("audio_occupancy_bytes", -1),
            lambda trace: trace["events"][10].__setitem__("duration_us", -1),
            lambda trace: trace["events"][11].__setitem__("outcome", "pass"),
        ]
        for probe in probes:
            self.assert_trace_rejected(probe)

        def zero_trace_loss(trace):
            trace["events"].insert(-1, {
                "event_id": "e-13",
                "event_name": "trace_loss",
                "t_us": 75,
                "provenance": "host",
                "phase": "NONE",
                "reason": "queue_overflow",
                "lost_event_count": 0,
            })

        self.assert_trace_rejected(zero_trace_loss)

    def test_mixed_types_never_escape_as_python_type_errors(self):
        probes = [
            lambda trace: trace["events"][1].__setitem__("event_id", []),
            lambda trace: trace["events"][1].__setitem__("event_name", []),
            lambda trace: trace["events"][1].__setitem__("t_us", "10"),
            lambda trace: trace["events"][1].__setitem__("provenance", {}),
            lambda trace: trace["events"][1].__setitem__("phase", {}),
            lambda trace: trace["events"][1].__setitem__("accepted", 1),
            lambda trace: trace["events"][8].__setitem__("points_processed", "1"),
            lambda trace: trace["events"][8].__setitem__("presentation_pending", 0),
            lambda trace: trace["events"][9].__setitem__("audio_continuity_status", []),
            lambda trace: trace["workload_metadata"].__setitem__("seed", {}),
            lambda trace: trace["workload_metadata"].__setitem__("timestamp_jitter_bound_us", 2001),
            lambda trace: trace["workload_metadata"].__setitem__("generator_version", 1),
            lambda trace: trace["workload_metadata"].__setitem__("declared_features", [None]),
        ]
        for probe in probes:
            self.assert_trace_rejected(probe)
        with self.assertRaises(metrics.MetricsError):
            metrics.load_json_bytes("{}")

    def test_trace_counts_only_true_touch_accepted_events(self):
        trace = trace_fixture()
        result = metrics.aggregate_trace(trace)
        self.assertEqual(result.metrics["accepted_point_count"].value, 1)
        self.assertEqual(result.metrics["accepted_point_count"].sample_count, 1)


class UnmeasuredEvidenceRegressionTest(unittest.TestCase):
    def test_unmeasured_numeric_values_and_samples_are_rejected(self):
        for wrapper in (
            {"value": 0, "unit": "us", "provenance": "unmeasured/blocked", "missing_reason": "not captured"},
            {"value": 0, "unit": "bytes", "provenance": "host"},
        ):
            with self.assertRaises(metrics.MetricsError):
                metrics.aggregate_observations(observation({
                    "capture_worker_busy_us" if wrapper["unit"] == "us" else "internal_free_bytes": wrapper,
                }))
        with self.assertRaises(metrics.MetricsError):
            metrics.aggregate_observations(observation({
                "ui_update_us": {
                    "samples": [0],
                    "unit": "us",
                    "provenance": "unmeasured/blocked",
                    "missing_reason": "no synchronized timing source",
                }
            }))

    def test_unmeasured_output_preserves_null_reason_and_has_no_percentiles(self):
        reason = "hardware capture was not available"
        result = metrics.aggregate_observations(observation({
            "ui_update_us": {
                "samples": [],
                "unit": "us",
                "provenance": "unmeasured/blocked",
                "missing_reason": reason,
            },
            "internal_free_bytes": {
                "value": None,
                "unit": "bytes",
                "provenance": "host",
                "missing_reason": reason,
            },
        }))
        for field in metrics.SAMPLE_SERIES["ui_update_us"]:
            record = result.metrics[field]
            self.assertIsNone(record.value)
            self.assertEqual(record.sample_count, 0)
            self.assertEqual(record.missing_reason, reason)
        hardware = result.metrics["internal_free_bytes"]
        self.assertIsNone(hardware.value)
        self.assertEqual(hardware.sample_count, 0)
        self.assertEqual(hardware.missing_reason, reason)

    def test_audio_component_hardware_zero_is_not_a_measured_pass(self):
        trace = trace_fixture()
        required = [
            field
            for group in ("audio", "drawing_and_concurrency", "memory", "cleanup_and_repetition")
            for field in metrics.FIELD_GROUPS[group]
        ]
        typed = {}
        for field in required:
            unit = metrics.FIELD_UNITS[field]
            is_hardware_only = field in metrics.HARDWARE_ONLY_FIELDS
            typed[field] = {
                "value": None if is_hardware_only else ("model-continuous" if field == "audio_continuity_status" else True if unit == "boolean" else 1),
                "unit": unit,
                "provenance": "host",
                "sample_count": 0 if is_hardware_only else 1,
                "missing_reason": "hardware not exercised" if is_hardware_only else None,
            }
        trace["audio_concurrency"] = {
            "session_token": 1,
            "scenario_id": "dense-drawing",
            "expected_outcome": "completed-with-drawing-pressure",
            "observed_outcome": "completed",
            "failure": "none",
            "expectation_met": True,
            "provenance": "host",
            "unknown_reason": "host model only",
            "hardware_audio_health_measured": False,
            "resources_released": True,
            "cleanup_timestamps_recorded": True,
            "terminal_timestamp_us": 80,
            "cleanup_timestamp_us": 87,
            "metrics": typed,
        }
        trace["audio_concurrency"]["metrics"]["internal_free_bytes"]["value"] = 0
        trace["audio_concurrency"]["metrics"]["internal_free_bytes"]["provenance"] = "unmeasured/blocked"
        with self.assertRaises(metrics.MetricsError):
            metrics.aggregate_trace(trace)


if __name__ == "__main__":
    unittest.main()
