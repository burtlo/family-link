"""Focused LAB-04 parser and adapter fail-closed checks."""
from __future__ import annotations

import sys
import tempfile
import unittest
import importlib.util
from types import ModuleType
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
_parser_spec = importlib.util.spec_from_file_location(
    "parse_lab04_trace", ROOT / "client/labs/lab-04-sketch-format/tools/parse_lab_trace.py")
parser = importlib.util.module_from_spec(_parser_spec)
sys.modules[_parser_spec.name] = parser
_parser_spec.loader.exec_module(parser)
from lab_adapter_lab04 import Adapter, _capture, _safe_line  # noqa: E402
from lab_registry import get_lab, run_mode_hint_lines  # noqa: E402
from project_config import ProjectConfigError  # noqa: E402


def _row(seq: int, scenario: str, variant: str, terminal: str, cls: str, **extra) -> str:
    fields = {"schema": "lab04-trace/v1", "event": "scenario_terminal", "seq": seq,
              "monotonic_us": seq * 1000,
              "scenario": scenario, "fixture": "corpus-b", "class": cls,
              "variant": variant, "checkpoint_position": "", "operation_id": seq + 10, "terminal": terminal,
              "terminal_count": 1, "reason": "ok", "raster_work_cap": 64,
              "raster_eligible": 0, "raster_rendered": 0, "raster_remaining": 0, **extra}
    return "LAB04_TRACE " + " ".join(f"{key}={value}" for key, value in fields.items())


def _serial(*, mode="scripted", decided=True, harness=True):
    build = "lab04-fw-v1-scripted" if mode == "scripted" else "lab04-fw-v1-manual"
    lines = [f"LAB04_READY build={build} mode={mode} server=none trace_schema=lab04-trace/v1"]
    seq = 1
    for scenario, variant in (("F-points", "points"), ("F-strokes", "strokes"), ("F-preview", "preview")):
        lines.append(_row(seq, scenario, variant, "success", "exploratory", encoded_bytes=128))
        seq += 1
    for scenario, terminal in (("F-malformed", "failure"), ("F-overflow", "failure"), ("F-cancel", "cancelled")):
        for variant in ("points", "strokes", "preview"):
            if decided:
                repetitions = (("truncated_stroke", "input_too_large", "wrong_color_count")
                               if scenario == "F-malformed" and variant == "strokes" else
                               ("truncated_records", "input_too_large", "wrong_color_count")
                               if scenario == "F-malformed" else
                               ("overflow",) if scenario == "F-overflow" else ("cancelled_mid_decode",))
                for reason in repetitions:
                    lines.append(_row(seq, scenario, variant, terminal, "decided", reason=reason))
                    seq += 1
    for scenario, terminal, variant in (("F-checkpoint", "unsupported", "points"), ("F-legacy", "success", "legacy")):
        lines.append(_row(seq, scenario, variant, terminal, "exploratory"))
        seq += 1
    if mode == "scripted" and harness:
        lines.append("LAB04_HARNESS_DONE status=complete variants=3 checkpoints=0")
    footer_lines = [
        "LAB04_TRACE schema=lab04-trace/v1 event=publication_loss source=firmware_publication_queue dropped_events=0",
        f"LAB04_TRACE schema=lab04-trace/v1 event=trace_summary expected_rows={seq - 1} publication_queue_drops=0 serial_write_errors=0",
    ]
    insert_at = next((i for i, line in enumerate(lines) if line.startswith("LAB04_HARNESS_DONE ")), len(lines))
    lines[insert_at:insert_at] = footer_lines
    return "\n".join(lines) + "\n"


def _legacy_serial(mode="scripted"):
    """Model the earlier successful v1 captures, which lack C-3 counters."""
    counter_fields = ("raster_work_cap", "raster_eligible", "raster_rendered", "raster_remaining")
    lines = []
    for line in _serial(mode=mode).splitlines():
        if "event=publication_loss" in line or "event=trace_summary" in line:
            continue
        for field in counter_fields:
            line = line.replace(f" {field}=64", "") if field == "raster_work_cap" else line
            line = line.replace(f" {field}=0", "") if field != "raster_work_cap" else line
        if line.startswith("LAB04_TRACE ") and "event=scenario_terminal" in line:
            line += " raster_us=17"
        lines.append(line)
    return "\n".join(lines) + "\n"


def _supplemental_serial(*, publication_drops=2):
    resource = {
        "schema": "lab04-trace/v1", "event": "resource_sample", "seq": 21,
        "monotonic_us": 1000, "scenario": "F-preview", "fixture": "corpus-b",
        "variant": "preview", "operation_id": 13, "sample_kind": "point",
        "phase": "post_decode_pre_raster", "image_resident": 1,
        "image_storage": "encoded_payload", "image_bytes": 28800,
        "decoded_resident": 1, "decoded_segments": 14, "ledger_b_image_resident": 1,
        "ledger_b_image_storage": "encoded_payload", "ledger_b_image_bytes": 28800,
        "ledger_b_image_caps": "MALLOC_CAP_DEFAULT",
        "ledger_b_image_lifetime": "encoded_result_live_through_raster",
        "ledger_b_decoded_resident": 1, "ledger_b_decoded_segments": 14,
        "ledger_b_decoded_capacity_bytes": 224, "internal_largest_free_block_bytes": 10000,
        "psram_largest_free_block_bytes": 20000, "lvgl_sample_status": "display_lock_busy",
    }
    row = "LAB04_TRACE " + " ".join(f"{key}={value}" for key, value in resource.items())
    publication = ("LAB04_TRACE schema=lab04-trace/v1 event=publication_loss "
                   f"source=firmware_publication_queue dropped_events={publication_drops}")
    footer = ("LAB04_TRACE schema=lab04-trace/v1 event=trace_summary "
              f"expected_rows=21 publication_queue_drops={publication_drops} serial_write_errors=0")
    marker = "LAB04_HARNESS_DONE status=complete"
    base = "\n".join(line for line in _serial().splitlines()
                       if "event=publication_loss" not in line and "event=trace_summary" not in line) + "\n"
    return base.replace(marker, row + "\n" + publication + "\n" + footer + "\n" + marker)


class Lab04ParserTests(unittest.TestCase):
    def test_help_marks_local_backend_unsupported(self):
        text = "\n".join(run_mode_hint_lines(get_lab("lab04")))
        self.assertIn("BACKEND=managed-http (required)", text)
        self.assertIn("local comparison is unsupported", text)
        self.assertNotIn("or local comparison", text)

    def test_redact_line_redacts_url_password_and_bearer(self):
        value = parser.redact_line("url=https://example.test/a password=hunter2 Authorization: Bearer abcdefghijkl")
        self.assertIn("[REDACTED_URL]", value)
        self.assertIn("password=[REDACTED]", value)
        self.assertIn("Bearer [REDACTED]", value)
        self.assertNotIn("hunter2", value)
        self.assertNotIn("abcdefghijkl", value)
        evidence_line = _safe_line(
            b"url=https://example.test/a password=hunter2 Authorization: Bearer abcdefghijkl\n", parser
        ).decode("utf-8")
        self.assertIn("[REDACTED_URL]", evidence_line)
        self.assertIn("password=[REDACTED]", evidence_line)
        self.assertIn("Bearer [REDACTED]", evidence_line)

    def test_scripted_success_and_exact_experiment_inputs(self):
        result = parser.parse(_serial(), "run-1", mode="scripted", variant="scripted")
        metrics = result["lab_metrics"]
        self.assertEqual(metrics["experiment_inputs"], ["F-malformed", "F-overflow", "F-cancel"])
        self.assertEqual(metrics["experiment_verdict"], "PASS")
        self.assertEqual(metrics["scenarios"]["F-points"]["observation"]["encoded_bytes"], 128)
        self.assertEqual(metrics["scenarios"]["F-malformed"]["sample_count"], 9)
        self.assertNotIn("selected_format", metrics)
        self.assertEqual(result["document"], "family-link.lab-metrics/v2")
        self.assertEqual(result["schema_version"], "lab04-trace/v1")
        self.assertEqual(result["parser_version"], "lab04-parser/v1")
        self.assertEqual(result["evidence"]["status"], "COMPLETE")
        self.assertEqual(result["evidence"]["trace_loss"], "UNKNOWN")
        self.assertEqual(result["evidence"]["firmware_trace_loss"]["dropped_events"], None)
        self.assertIn("firmware_trace_loss", result["evidence"]["missing"])

    def test_bounded_raster_counters_are_preserved_as_validated_integers(self):
        trace = _serial().replace(
            "fixture=corpus-b class=exploratory variant=points",
            "fixture=corpus-a-dense class=exploratory variant=points", 1)
        trace = trace.replace(
            "raster_work_cap=64 raster_eligible=0 raster_rendered=0 raster_remaining=0",
            "raster_work_cap=64 raster_eligible=4096 raster_rendered=64 raster_remaining=4032", 1)
        result = parser.parse(trace, "run-1", mode="scripted")
        row = result["lab_metrics"]["scenarios"]["F-points"]["samples"][0]
        self.assertEqual((row["raster_work_cap"], row["raster_eligible"],
                          row["raster_rendered"], row["raster_remaining"]), (64, 4096, 64, 4032))
        self.assertTrue(all(isinstance(row[key], int) for key in
                            ("raster_work_cap", "raster_eligible", "raster_rendered", "raster_remaining")))
        for bad in ("raster_work_cap=65", "raster_rendered=65", "raster_remaining=4031"):
            malformed = trace.replace("raster_work_cap=64", bad, 1) if bad.startswith("raster_work_cap") else trace.replace(
                "raster_rendered=64", bad, 1) if bad.startswith("raster_rendered") else trace.replace(
                    "raster_remaining=4032", bad, 1)
            with self.subTest(bad=bad), self.assertRaises(parser.ParseError):
                parser.parse(malformed, "run-1", mode="scripted")

    def test_legacy_manual_and_scripted_captures_preserve_decided_results(self):
        for mode in ("manual", "scripted"):
            with self.subTest(mode=mode):
                result = parser.parse(_legacy_serial(mode), "legacy-run", mode=mode,
                                      capture_context={"kind": "device_serial_capture",
                                                       "target_id": "kit-123", "post_reset": True})
                metrics = result["lab_metrics"]
                self.assertEqual(metrics["experiment_inputs"], ["F-malformed", "F-overflow", "F-cancel"])
                self.assertEqual(metrics["experiment_verdict"], "INCOMPLETE")
                self.assertEqual(metrics["performance_verdict"], "UNMEASURED")
                self.assertEqual(result["evidence_provenance"], "hardware")
                self.assertEqual(result["evidence"]["trace_loss"], "UNKNOWN")
                self.assertEqual(metrics["raster_work_accounting"]["status"], "MISSING")
                self.assertIn("raster_work_accounting", result["evidence"]["missing"])
                self.assertEqual(metrics["measurements"]["raster_us"]["sample_count"], 0)
                self.assertIn("eligibility cannot be established",
                              metrics["measurements"]["raster_us"]["missing_reason"])
                rows = result["trace_rows"]
                self.assertEqual(rows, 20)
                first = metrics["scenarios"]["F-points"]["samples"][0]
                self.assertNotIn("raster_eligible", first)
                self.assertEqual(metrics["scenarios"]["F-malformed"]["sample_count"], 9)
                self.assertNotIn("raster_work_accounting", metrics["experiment_inputs"])

    def test_partial_legacy_raster_counter_sets_are_rejected(self):
        serial = _serial()
        for field in ("raster_work_cap", "raster_eligible", "raster_rendered", "raster_remaining"):
            partial = serial.replace(f" {field}=" + ("64" if field == "raster_work_cap" else "0"), "", 1)
            with self.subTest(field=field), self.assertRaisesRegex(
                    parser.ParseError, "incomplete bounded raster accounting fields"):
                parser.parse(partial, "run-1", mode="scripted")

    def test_measurements_use_nearest_rank_and_exclude_no_work_raster_rows(self):
        rows = [{"scenario": "F-points", "encoded_bytes": 128,
                 "encode_us": value, "decode_us": value, "raster_us": value,
                 "internal_largest_free_block_bytes": 1000 + value,
                 "terminal": "success",
                 "raster_work_cap": 64, "raster_eligible": 10, "raster_rendered": 1,
                 "raster_remaining": 9}
                for value in range(1, 21)]
        rows.append({"scenario": "F-overflow", "encode_us": 0, "decode_us": 0,
                     "seek_us": 0, "internal_largest_free_block_bytes": 0,
                     "raster_us": 0, "raster_work_cap": 64, "raster_eligible": 0,
                     "raster_rendered": 0, "raster_remaining": 0})
        rows.append({"raster_us": 2, "raster_work_cap": 64, "raster_eligible": 10,
                     "raster_rendered": 0, "raster_remaining": 10,
                     "terminal": "failure"})
        rows.append({"scenario": "F-checkpoint", "seek_us": 7})
        measurements = parser._measurement_series(rows, "hardware")
        encode = measurements["encode_us"]
        self.assertEqual((encode["p50"], encode["p95"], encode["p99"], encode["worst"]),
                         (10, 19, 20, 20))
        self.assertEqual(encode["sample_count"], 20)
        self.assertEqual(encode["unit"], "us")
        self.assertEqual(encode["provenance"], "hardware")
        raster = measurements["raster_us"]
        self.assertEqual(raster["sample_count"], 20)
        self.assertEqual(raster["samples"], list(range(1, 21)))
        self.assertEqual(measurements["seek_us"]["samples"], [7])
        self.assertEqual(measurements["internal_largest_free_block_bytes"]["sample_count"], 20)
        self.assertEqual(measurements["internal_largest_free_block_bytes"]["samples"],
                         list(range(1001, 1021)))

    def test_empty_measurements_are_null_and_parser_provenance_defaults_to_host(self):
        empty = parser._measurement_series([], "host")
        for name, series in empty.items():
            with self.subTest(name=name):
                self.assertEqual(series["sample_count"], 0)
                self.assertEqual(series["provenance"], "unmeasured/blocked")
                self.assertEqual([series[key] for key in ("p50", "p95", "p99", "worst")],
                                 [None, None, None, None])
                self.assertEqual(series["missing_reason"], "no eligible samples in trace")
        result = parser.parse(_serial(), "run-1", mode="scripted")
        self.assertEqual(result["evidence_provenance"], "host")
        self.assertEqual(result["provenance_context"]["kind"], "offline_or_fixture_parse")
        self.assertEqual(result["lab_metrics"]["performance_verdict"], "UNMEASURED")

    def test_explicit_device_capture_context_sets_hardware_provenance(self):
        result = parser.parse(_serial(), "run-1", mode="scripted", capture_context={
            "kind": "device_serial_capture", "target_id": "kit-123", "post_reset": True})
        self.assertEqual(result["evidence_provenance"], "hardware")
        self.assertEqual(result["provenance_context"], {
            "kind": "device_serial_capture", "target_id": "kit-123", "post_reset": True})
        with self.assertRaises(parser.ParseError):
            parser.parse(_serial(), "run-1", mode="scripted", capture_context={
                "kind": "device_serial_capture", "target_id": "kit-123", "post_reset": False})

    def test_decided_inventory_is_required_for_pass_and_wrong_variants_fail_closed(self):
        serial = _serial()
        # Remove two expected F-overflow repetitions while preserving the marker.
        lines = [line for line in serial.splitlines()
                 if not ("scenario=F-overflow" in line and "variant=" in line
                         and ("variant=points" in line or "variant=strokes" in line))]
        partial = parser.parse("\n".join(lines) + "\n", "run-1", mode="scripted")
        self.assertEqual(partial["lab_metrics"]["experiment_verdict"], "INCOMPLETE")
        self.assertEqual(partial["lab_metrics"]["scenarios"]["F-overflow"]["sample_count"], 1)
        wrong = serial.replace("scenario=F-overflow fixture=corpus-b class=decided variant=points",
                               "scenario=F-overflow fixture=corpus-b class=decided variant=legacy", 1)
        result = parser.parse(wrong, "run-1", mode="scripted")
        self.assertNotEqual(result["lab_metrics"]["experiment_verdict"], "PASS")

    def test_reported_firmware_loss_is_parsed(self):
        serial = _serial().replace("LAB04_HARNESS_DONE status=complete",
                                   "LAB04_TRACE schema=lab04-trace/v1 event=trace_loss dropped_events=2 oversized_events=1 malformed_rows=0\nLAB04_HARNESS_DONE status=complete")
        evidence = parser.parse(serial, "run-1", mode="scripted")["evidence"]
        self.assertEqual(evidence["firmware_trace_loss"], {
            "status": "OBSERVED", "dropped_events": 2, "oversized_events": 1, "malformed_rows": 0})
        self.assertEqual(evidence["trace_loss"], "OBSERVED")
        self.assertNotIn("firmware_trace_loss", evidence["missing"])
        zero_loss_serial = _serial().replace(
            "LAB04_HARNESS_DONE status=complete",
            "LAB04_TRACE schema=lab04-trace/v1 event=trace_loss dropped_events=0 oversized_events=0 malformed_rows=0\nLAB04_HARNESS_DONE status=complete")
        self.assertEqual(parser.parse(zero_loss_serial, "run-1", mode="scripted")["evidence"]["trace_loss"], "NONE")

    def test_supplemental_resource_and_publication_loss_are_distinct(self):
        parsed = parser.parse(_supplemental_serial(), "run-1", mode="scripted")
        evidence = parsed["evidence"]
        metrics = parsed["lab_metrics"]
        self.assertEqual(evidence["trace_loss"], "OBSERVED")
        self.assertEqual(evidence["firmware_trace_loss"]["status"], "UNKNOWN")
        self.assertEqual(evidence["firmware_publication_queue"], {
            "status": "OBSERVED", "source": "firmware_publication_queue", "dropped_events": 2})
        samples = metrics["device_resource_samples"]
        self.assertEqual((samples["status"], samples["measurement_kind"], samples["sample_count"]),
                         ("OBSERVED", "point_observation_not_peak", 1))
        self.assertEqual(samples["samples"][0]["ledger_b_image_bytes"], 28800)
        self.assertEqual(metrics["performance_verdict"], "UNMEASURED")

    def test_summary_reconciles_rows_and_does_not_conflate_queue_and_serial_losses(self):
        baseline = _serial()
        no_summary = "\n".join(line for line in baseline.splitlines() if "event=trace_summary" not in line) + "\n"
        parsed = parser.parse(no_summary, "run-1", mode="scripted")
        self.assertEqual(parsed["evidence"]["trace_loss"], "UNKNOWN")
        self.assertEqual(parsed["evidence"]["status"], "INCOMPLETE")
        self.assertEqual(parsed["lab_metrics"]["experiment_verdict"], "INCOMPLETE")
        malformed = baseline.replace("serial_write_errors=0", "serial_write_errors=nope")
        self.assertEqual(parser.parse(malformed, "run-1", mode="scripted")["evidence"]["trace_summary"]["status"], "MALFORMED")
        malformed_row = baseline.replace("event=trace_summary expected_rows=20", "event=trace_summary  expected_rows=20")
        self.assertEqual(parser.parse(malformed_row, "run-1", mode="scripted")["evidence"]["trace_summary"]["status"], "MALFORMED")
        footer = "LAB04_TRACE schema=lab04-trace/v1 event=trace_summary expected_rows=20 publication_queue_drops=0 serial_write_errors=0"
        reordered = baseline.replace(footer + "\n", "").replace("event=publication_loss", footer + "\nevent=publication_loss")
        self.assertNotEqual(parser.parse(reordered, "run-1", mode="scripted")["evidence"]["trace_reconciliation"]["status"], "COMPLETE")
        for label, changed in (
            ("tail_gap", baseline.replace("seq=20 ", "seq=22 ", 1)),
            ("interior_gap", baseline.replace("seq=10 ", "seq=22 ", 1)),
            ("wrong_count", baseline.replace("expected_rows=20", "expected_rows=21")),
        ):
            with self.subTest(label=label):
                result = parser.parse(changed, "run-1", mode="scripted")
                self.assertNotEqual(result["lab_metrics"]["experiment_verdict"], "PASS")
                self.assertNotEqual(result["evidence"]["trace_reconciliation"]["status"], "COMPLETE")
        with self.assertRaisesRegex(parser.ParseError, "duplicate trace sequence"):
            parser.parse(baseline.replace("seq=20 ", "seq=19 ", 1), "run-1", mode="scripted")
        write_error = baseline.replace("serial_write_errors=0", "serial_write_errors=1")
        write_result = parser.parse(write_error, "run-1", mode="scripted")
        self.assertEqual(write_result["evidence"]["trace_reconciliation"]["status"], "COMPLETE")
        self.assertEqual(write_result["evidence"]["trace_loss"], "OBSERVED")
        self.assertEqual(write_result["evidence"]["trace_summary"]["serial_write_errors"], 1)
        queue_drop = baseline.replace("dropped_events=0", "dropped_events=2").replace(
            "publication_queue_drops=0", "publication_queue_drops=2")
        queue_result = parser.parse(queue_drop, "run-1", mode="scripted")
        self.assertEqual(queue_result["evidence"]["trace_reconciliation"]["status"], "COMPLETE")
        self.assertEqual(queue_result["evidence"]["trace_loss"], "OBSERVED")
        self.assertEqual(queue_result["evidence"]["trace_summary"]["serial_write_errors"], 0)

    def test_resource_peak_is_reservation_observation(self):
        peak = ("LAB04_TRACE schema=lab04-trace/v1 event=resource_peak seq=21 monotonic_us=1000 "
                "scenario=F-preview fixture=corpus-b variant=preview operation_id=13 sample_kind=peak "
                "phase=maximum_output_capacity_reserved_before_record_insertion_encoded_payload_live "
                "ledger_b_image_resident=1 ledger_b_image_storage=encoded_payload ledger_b_image_bytes=28800 "
                "ledger_b_image_caps=MALLOC_CAP_DEFAULT ledger_b_image_lifetime=encoded_result_live_through_raster "
                "ledger_b_decoded_resident=1 ledger_b_expected_output_segments=14 "
                "ledger_b_decoded_capacity_bytes=224 image_resident=1 image_storage=encoded_payload "
                "image_bytes=28800 decoded_resident=1 expected_output_segments=14 decoded_capacity_bytes=224 "
                "internal_largest_free_block_bytes=10000 psram_largest_free_block_bytes=20000 "
                "lvgl_sample_status=display_lock_busy")
        serial = _serial().replace("event=trace_summary expected_rows=20", "event=trace_summary expected_rows=21")
        publication = "LAB04_TRACE schema=lab04-trace/v1 event=publication_loss source=firmware_publication_queue dropped_events=0"
        serial = serial.replace(publication, peak + "\n" + publication)
        metrics = parser.parse(serial, "run-1", mode="scripted")["lab_metrics"]["device_resource_peak"]
        self.assertEqual(metrics["status"], "OBSERVED")
        self.assertEqual(metrics["measurement_kind"], "decoder_maximum_output_capacity_reservation")
        self.assertFalse(metrics["whole_system_temporal_peak"])
        self.assertEqual(metrics["samples"][0]["expected_output_segments"], 14)
        malformed = serial.replace("decoded_capacity_bytes=224", "decoded_capacity_bytes=223", 1)
        with self.assertRaises(parser.ParseError):
            parser.parse(malformed, "run-1", mode="scripted")
        orphan = serial.replace("operation_id=13", "operation_id=999", 1)
        with self.assertRaisesRegex(parser.ParseError, "does not match one successful preview"):
            parser.parse(orphan, "run-1", mode="scripted")
        duplicate_peak = peak.replace("seq=21", "seq=22")
        duplicate_serial = serial.replace("expected_rows=21", "expected_rows=22").replace(
            publication, duplicate_peak + "\n" + publication)
        with self.assertRaisesRegex(parser.ParseError, "duplicate resource peak for operation"):
            parser.parse(duplicate_serial, "run-1", mode="scripted")
        preview_terminal = "operation_id=13 terminal=success"
        failed_terminal = serial.replace(preview_terminal, "operation_id=13 terminal=failure", 1)
        wrong_variant_lines = []
        for line in serial.splitlines():
            if "event=scenario_terminal" in line and "operation_id=13" in line:
                line = line.replace("variant=preview", "variant=points", 1)
            wrong_variant_lines.append(line)
        for bad_terminal in (failed_terminal, "\n".join(wrong_variant_lines) + "\n"):
            with self.assertRaisesRegex(parser.ParseError, "does not match one successful preview"):
                parser.parse(bad_terminal, "run-1", mode="scripted")

    def test_supplemental_events_reject_duplicates_overflow_and_identity_mismatch(self):
        serial = _supplemental_serial()
        resource = next(line for line in serial.splitlines() if "event=resource_sample" in line)
        publication = next(line for line in serial.splitlines() if "event=publication_loss" in line)
        cases = (
            serial.replace(publication, publication + "\n" + publication),
            serial.replace(resource, resource + "\n" + resource),
            serial.replace("dropped_events=2", "dropped_events=4294967296"),
            serial.replace("ledger_b_image_bytes=28800", "ledger_b_image_bytes=28799"),
            serial.replace("phase=post_decode_pre_raster", "phase=decode"),
        )
        for case in cases:
            with self.subTest(case=case[-160:]), self.assertRaises(parser.ParseError):
                parser.parse(case, "run-1", mode="scripted")

    def test_scripted_completion_accepts_checkpoints_zero_or_one_only(self):
        for checkpoints in ("0", "1"):
            with self.subTest(checkpoints=checkpoints):
                trace = _serial().replace("checkpoints=0", f"checkpoints={checkpoints}")
                self.assertEqual(parser.parse(trace, "run-1", mode="scripted")["lab_metrics"]["experiment_verdict"], "PASS")
        invalid = _serial().replace("checkpoints=0", "checkpoints=2")
        with self.assertRaises(parser.ParseError):
            parser.parse(invalid, "run-1", mode="scripted")

    def test_exploratory_rows_are_retained_but_never_enter_inputs(self):
        result = parser.parse(_serial(), "run-1", mode="scripted")
        rows = result["lab_metrics"]["scenarios"]
        for key in ("F-points", "F-strokes", "F-preview", "F-checkpoint", "F-legacy"):
            self.assertEqual(rows[key]["class"], "exploratory")
            self.assertIn("observation", rows[key])
            self.assertNotIn(key, result["lab_metrics"]["experiment_inputs"])

    def test_parser_rejects_missing_duplicate_overflow_and_sensitive_trace(self):
        cases = [
            "LAB04_READY build=lab04-fw-v1-manual mode=manual server=none trace_schema=lab04-trace/v1\n",
            _serial().replace("seq=1", "seq=1 seq=1", 1),
            _serial().replace("seq=1", "seq=4294967296", 1),
            _serial() + "PIN=1234\n",
            _serial() + "LAB04_TRACE schema=lab04-trace/v1 event=media_payload payload_b64=abc\n",
        ]
        for value in cases:
            with self.subTest(value=value[-80:]), self.assertRaises(parser.ParseError):
                parser.parse(value, "run-1", mode="scripted")

    def test_parser_rejects_pre_reset_marker_missing_harness_and_decided_samples(self):
        with self.assertRaises(parser.ParseError):
            parser.parse(_serial(), "run-1", mode="scripted", post_reset=False)
        with self.assertRaises(parser.ParseError):
            parser.parse(_serial(harness=False), "run-1", mode="scripted")
        with self.assertRaises(parser.ParseError):
            parser.parse(_serial(decided=False), "run-1", mode="scripted")
        with self.assertRaises(parser.ParseError):
            parser.parse(_serial(), "run-1", mode="scripted", variant="manual")

    def test_parser_rejects_mode_mismatch_and_unsupported_terminal_evidence(self):
        with self.assertRaises(parser.ParseError):
            parser.parse(_serial(mode="manual"), "run-1", mode="scripted")
        serial = _serial()
        wrong = serial
        overflow = next(line for line in wrong.splitlines() if "scenario=F-overflow" in line and "variant=points" in line)
        wrong = wrong.replace(overflow, overflow.replace("terminal=failure", "terminal=success"))
        result = parser.parse(wrong, "run-1", mode="scripted")
        self.assertEqual(result["lab_metrics"]["experiment_verdict"], "FAIL")

    def test_exploratory_rows_never_change_decided_verdict(self):
        inputs = ["F-malformed", "F-overflow", "F-cancel"]
        decided_failure = {key: {"terminal_count": 1, "terminal": terminal}
                           for key, terminal in (("F-malformed", "success"),
                                                 ("F-overflow", "failure"),
                                                 ("F-cancel", "cancelled"))}
        decided_success = {key: {"terminal_count": 1, "terminal": terminal}
                           for key, terminal in (("F-malformed", "failure"),
                                                 ("F-overflow", "failure"),
                                                 ("F-cancel", "cancelled"))}
        decided_failure["F-points"] = {"class": "exploratory", "observation": {"terminal": "success"}}
        decided_success["F-points"] = {"class": "exploratory", "observation": {"terminal": "failure"}}
        self.assertEqual(parser.classify_experiment(decided_failure, inputs), "FAIL")
        self.assertEqual(parser.classify_experiment(decided_success, inputs), "PASS")
        self.assertEqual(parser.classify_experiment(decided_success, []), "UNMEASURED")


class Lab04AdapterTests(unittest.TestCase):
    def _inputs(self, backend="managed-http", mode="scripted", variant=None):
        spec = SimpleNamespace(variants={"manual": object(), "scripted": object()},
                               default_variant="manual", capture_seconds_default=30,
                               trace_parser=ROOT / "client/labs/lab-04-sketch-format/tools/parse_lab_trace.py",
                               lab_id="LAB-04", evidence_slug="lab-04", project_dir=ROOT / "client/labs/lab-04-sketch-format",
                               target="esp32s3", make_name="lab04")
        args = SimpleNamespace(backend=backend, mode=mode, variant=variant, seconds=1,
                               kit="", port="", run_id="test-run", no_build=False)
        return spec, args

    def test_backend_and_variant_mismatch_reject_before_io(self):
        spec, args = self._inputs(backend="local")
        with patch("lab_adapter_lab04._parser_module", side_effect=AssertionError("I/O path reached")):
            with self.assertRaises(ProjectConfigError):
                Adapter().run(None, None, spec, args, mode="scripted")
        spec, args = self._inputs(variant="manual")
        with self.assertRaises(ProjectConfigError):
            Adapter().run(None, None, spec, args, mode="scripted")
        spec, args = self._inputs()
        code, captured = self._execute(spec, args, _serial())
        self.assertEqual(captured["variant"], "scripted")
        self.assertEqual(code, 0)

    def test_adapter_marks_its_reset_gated_target_capture_as_hardware(self):
        spec, args = self._inputs()
        _, captured = self._execute(spec, args, _serial())
        self.assertEqual(captured["metrics"]["evidence_provenance"], "hardware")
        self.assertEqual(captured["metrics"]["provenance_context"], {
            "kind": "device_serial_capture", "target_id": "kit", "post_reset": True})
        firmware = captured["context"]["firmware"]
        for key in ("build_id", "build_fingerprint", "source_sha256", "sdkconfig_sha256",
                    "binary_sha256", "fixture_sha256"):
            self.assertTrue(firmware[key], key)
        self.assertEqual(firmware["build_id"], "lab04-fw-v1-scripted")

    def _execute(self, spec, args, serial, cleanup_failure=False, capture_error=None):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            serial_path = root / "serial.txt"
            serial_path.write_text(serial)
            host = SimpleNamespace(ROOT=root,
                                   _flash_from_env_and_args=lambda *_a, **_k: False,
                                   resolve_run_target=lambda *_a: SimpleNamespace(kit_id="kit", device="port"),
                                   resolve_variant=lambda *_a: SimpleNamespace(build_dir="build"))
            def build_lab(_config, _name, variant, **_kwargs):
                build_dir = root / "build"
                build_dir.mkdir(parents=True, exist_ok=True)
                build_id = f"lab04-fw-v1-{variant}"
                script_config = ("CONFIG_LAB04_SCRIPTED=y\n" if variant == "scripted"
                                 else "# CONFIG_LAB04_SCRIPTED is not set\n")
                (build_dir / "sdkconfig").write_text(
                    f'CONFIG_LAB04_BUILD_ID="{build_id}"\n' + script_config, encoding="utf-8")
                (build_dir / "lab04_sketch_format.bin").write_bytes(b"test firmware")
                return build_dir
            host.build_lab = build_lab
            class Evidence:
                run_dir = root / "evidence"
                def __init__(self):
                    self.run_dir.mkdir()
                    self.context = {}
                def update_context(self, facts): self.context.update(facts)
                def event_sink(self, _event): pass
            evidence = Evidence()
            context = SimpleNamespace(run_id="test-run")
            captured = {}
            def runner(**kwargs):
                captured["variant"] = kwargs["variant"]
                captured["result"] = kwargs["execute"](context, evidence)
                captured["context"] = evidence.context
                metrics_path = evidence.run_dir / "metrics.json"
                if metrics_path.exists():
                    captured["metrics"] = __import__("json").loads(metrics_path.read_text())
                return 0 if captured["result"]["workflow_status"] == "PASS" else 1
            capture_patch = (patch("lab_adapter_lab04._capture", side_effect=capture_error)
                             if capture_error is not None else
                             patch("lab_adapter_lab04._capture", return_value=serial_path))
            with capture_patch, \
                 patch("lab_adapter_lab04._cleanup", side_effect=RuntimeError("cleanup" ) if cleanup_failure else None), \
                 patch("lab_adapter_lab04.run_with_adapter", side_effect=runner):
                code = Adapter().run(host, None, spec, args, mode="scripted")
            return code, captured

    def test_cleanup_failure_and_malformed_or_missing_harness_never_pass(self):
        spec, args = self._inputs()
        _, captured = self._execute(spec, args, _serial(), cleanup_failure=True)
        self.assertEqual(captured["result"]["workflow_status"], "FAIL")
        self.assertFalse(captured["result"]["cleanup_succeeded"])
        for serial in (_serial(harness=False), _serial(decided=False), _serial().replace("seq=1", "seq=4294967296", 1)):
            _, captured = self._execute(spec, args, serial)
            self.assertNotEqual(captured["result"]["workflow_status"], "PASS")

    def test_adapter_validates_boot_build_id_against_resolved_sdkconfig(self):
        spec, args = self._inputs()
        wrong = _serial().replace("build=lab04-fw-v1-scripted", "build=not-this-build", 1)
        _, captured = self._execute(spec, args, wrong)
        self.assertEqual(captured["result"]["workflow_status"], "FAIL")
        self.assertFalse(captured["result"]["caller_completed"])

    def test_no_build_cannot_claim_a_source_bound_run_identity(self):
        spec, args = self._inputs()
        args.no_build = True
        _, captured = self._execute(spec, args, _serial())
        self.assertEqual(captured["result"]["workflow_status"], "FAIL")
        self.assertFalse(captured["result"]["caller_completed"])

    def test_adapter_preserves_unknown_or_parsed_firmware_loss(self):
        spec, args = self._inputs()
        _, absent = self._execute(spec, args, _serial())
        self.assertEqual(absent["context"]["event_loss"]["firmware_trace"]["status"], "UNKNOWN")
        self.assertIsNone(absent["context"]["event_loss"]["firmware_trace"]["dropped_events"])
        reported_serial = _serial().replace(
            "LAB04_HARNESS_DONE status=complete",
            "LAB04_TRACE schema=lab04-trace/v1 event=trace_loss dropped_events=2 oversized_events=1 malformed_rows=0\nLAB04_HARNESS_DONE status=complete")
        _, reported = self._execute(spec, args, reported_serial)
        self.assertEqual(reported["context"]["event_loss"]["firmware_trace"]["status"], "OBSERVED")
        self.assertEqual(reported["context"]["event_loss"]["firmware_trace"]["dropped_events"], 2)

    def test_sensitive_serial_lines_are_rejected_before_evidence_write_and_fail_run(self):
        class FakeSerial:
            def __init__(self, _device, **_kwargs):
                self.dtr = False
                self.rts = False
                self.closed = False

            def reset_input_buffer(self):
                pass

            def read(self, _size):
                return self.payload

            def close(self):
                self.closed = True

        for unsafe in (b"PIN=1234\n", b"LAB04_TRACE payload_b64=supersecretpayload\n"):
            with self.subTest(unsafe=unsafe), tempfile.TemporaryDirectory() as tmp:
                fake = FakeSerial("port")
                fake.payload = unsafe
                host = SimpleNamespace(ROOT=Path(tmp))
                serial_module = ModuleType("serial")
                serial_module.Serial = lambda *args, **kwargs: fake
                with patch.dict(sys.modules, {"serial": serial_module}), \
                     patch("lab_adapter_lab04.time.sleep"):
                    with self.assertRaises(parser.ParseError):
                        _capture(host, SimpleNamespace(device="port"), "run", 1, "scripted", "scripted", parser)
                evidence_path = Path(tmp) / "logs/device/labs/lab-04/run/serial.txt"
                self.assertEqual(evidence_path.read_bytes(), b"")
                self.assertNotIn(b"1234", evidence_path.read_bytes())
                self.assertNotIn(b"supersecretpayload", evidence_path.read_bytes())

        spec, args = self._inputs()
        _, captured = self._execute(spec, args, _serial(), capture_error=parser.ParseError("sensitive serial line"))
        self.assertEqual(captured["result"]["workflow_status"], "FAIL")
        self.assertFalse(captured["result"]["caller_completed"])


if __name__ == "__main__":
    unittest.main()
