"""Focused L1 tests for the additive LAB-02 C5a parser slice."""

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "parse_lab_trace",
    ROOT / "client/labs/lab-02-bounded-inbox/tools/parse_lab_trace.py",
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)
parse_trace = MODULE.parse_trace


class Lab02TraceMetricsTests(unittest.TestCase):
    def test_additive_context_and_nearest_rank_latency_preserve_legacy_fields(self):
        metrics = parse_trace([
            "LAB02_READY build=abc123 fixture=inbox_64 mode=recycle\n",
            "LAB_TRACE t_us=100 event=snap_start gen=1 focus=2 msg=- "
            "trace_schema=lab-trace/v1 run_id=run-1 session_id=7 snap_id=1\n",
            "LAB_TRACE t_us=1100 event=snap_end gen=1 focus=2 msg=- "
            "trace_schema=lab-trace/v1 run_id=run-1 session_id=7 snap_id=1 "
            "elapsed_us=1000 outcome=success\n",
            "LAB_TRACE t_us=2000 event=snap_start gen=1 focus=3 msg=- "
            "trace_schema=lab-trace/v1 run_id=run-1 session_id=7 snap_id=2\n",
            "LAB_TRACE t_us=5000 event=snap_end gen=1 focus=3 msg=- "
            "trace_schema=lab-trace/v1 run_id=run-1 session_id=7 snap_id=2 "
            "elapsed_us=3000 outcome=success\n",
        ])
        self.assertEqual(metrics["document"], "family-link.lab-metrics/v1")
        self.assertEqual(metrics["event_counts"]["snap_start"], 2)
        self.assertEqual(metrics["event_counts"]["snap_end"], 2)
        self.assertEqual(metrics["snap_events"], 4)
        self.assertEqual(metrics["run_id"], "run-1")
        self.assertEqual(metrics["metadata"]["fixture_size"], 64)
        self.assertEqual(metrics["metadata"]["build_id"], "abc123")
        self.assertIsNone(metrics["metadata"]["backend"])
        snap = metrics["distributions"]["snap_ms"]
        self.assertEqual(
            (snap["n"], snap["p50"], snap["p95"], snap["p99"], snap["worst"]),
            (2, 1.0, 3.0, 3.0, 3.0),
        )
        self.assertEqual(metrics["counts"]["snap_success"], 2)
        self.assertEqual(metrics["trace_loss"]["status"], "UNKNOWN")
        self.assertEqual(metrics["evidence"]["status"], "INCOMPLETE")
        self.assertIn("trace_loss", metrics["evidence"]["missing"])

    def test_absent_values_are_null_or_empty_not_zero(self):
        metrics = parse_trace([
            "LAB_TRACE t_us=1 event=bind gen=1 focus=0 msg=msg-1 "
            "trace_schema=lab-trace/v1 session_id=1 revision=2 state=bound\n",
        ])
        self.assertEqual(metrics["counts"]["bind"], 1)
        self.assertIsNone(metrics["counts"]["unbind"])
        self.assertEqual(metrics["distributions"]["bind_ms"]["n"], 0)
        self.assertIsNone(metrics["distributions"]["snap_ms"])
        self.assertIsNone(metrics["counts"]["trace_dropped"])
        self.assertEqual(metrics["evidence"]["timing_status"], "UNMEASURED")
        self.assertEqual(metrics["evidence"]["visual_status"], "UNMEASURED")

    def test_malformed_duplicate_orphan_unterminated_and_loss_are_accounted(self):
        metrics = parse_trace([
            "LAB_TRACE t_us=1 event=fetch_admitted gen=1 focus=0 msg=- "
            "trace_schema=lab-trace/v1 run_id=r session_id=1 op_kind=http_fetch op_id=9 attempt=1\n",
            "LAB_TRACE t_us=3 event=fetch_terminal gen=1 focus=0 msg=- "
            "trace_schema=lab-trace/v1 run_id=r session_id=1 op_kind=http_fetch op_id=9 attempt=1 "
            "elapsed_us=2000 outcome=success\n",
            "LAB_TRACE t_us=4 event=fetch_terminal gen=1 focus=0 msg=- "
            "trace_schema=lab-trace/v1 run_id=r session_id=1 op_kind=http_fetch op_id=9 attempt=1 "
            "elapsed_us=2000 outcome=success\n",
            "LAB_TRACE t_us=5 event=fetch_terminal gen=1 focus=0 msg=- "
            "trace_schema=lab-trace/v1 run_id=r session_id=1 op_kind=http_fetch op_id=10 attempt=1 "
            "elapsed_us=1 outcome=failure\n",
            "LAB_TRACE t_us=6 event=fetch_admitted gen=1 focus=0 msg=- "
            "trace_schema=lab-trace/v1 run_id=r session_id=1 op_kind=http_fetch op_id=11 attempt=1\n",
            "LAB_TRACE t_us=not-a-time event=fetch_terminal gen=1 focus=0 msg=-\n",
            "LAB_TRACE_LOSS status=OBSERVED dropped_events=2 oversized_events=1 malformed_rows=0\n",
        ])
        self.assertEqual(metrics["counts"]["malformed_trace"], 1)
        self.assertEqual(metrics["counts"]["duplicate_terminal"], 1)
        self.assertEqual(metrics["counts"]["orphan_terminal"], 1)
        self.assertEqual(metrics["counts"]["unterminated_operation"], 1)
        self.assertEqual(metrics["trace_loss"], {
            "status": "OBSERVED", "dropped_events": 2,
            "oversized_events": 1, "malformed_rows": 1,
        })
        self.assertEqual(metrics["counts"]["trace_dropped"], 2)
        self.assertEqual(metrics["evidence"]["status"], "INCOMPLETE")
        self.assertTrue(any("orphan_terminal" in value for value in metrics["evidence"]["invalid"]))
        self.assertEqual(metrics["distributions"]["fetch_attempt_ms"]["n"], 0)
        self.assertIsNone(metrics["distribution_outcomes"].get("fetch_attempt_ms"))

        snap_metrics = parse_trace([
            "LAB_TRACE t_us=1 event=snap_end gen=1 focus=0 msg=- "
            "trace_schema=lab-trace/v1 run_id=r session_id=1 snap_id=3 "
            "elapsed_us=9000 outcome=success\n",
            "LAB_TRACE t_us=2 event=snap_end gen=1 focus=0 msg=- "
            "trace_schema=lab-trace/v1 run_id=r session_id=1 snap_id=4 "
            "elapsed_us=8000 outcome=success\n",
        ])
        self.assertEqual(snap_metrics["distributions"]["snap_ms"]["n"], 0)
        self.assertIsNone(snap_metrics["distribution_outcomes"].get("snap_ms"))

    def test_negative_elapsed_is_excluded_and_marked_invalid(self):
        metrics = parse_trace([
            "LAB_TRACE t_us=1 event=page_apply gen=1 focus=0 msg=- "
            "trace_schema=lab-trace/v1 elapsed_us=-4\n",
        ])
        self.assertEqual(metrics["distributions"]["page_apply_ms"]["n"], 0)
        self.assertIn("page_apply_ms.negative_or_nonmonotonic_elapsed",
                      metrics["evidence"]["invalid"])
        self.assertEqual(metrics["evidence"]["timing_status"], "INCOMPLETE")

    def test_boolean_context_and_session_reset_violation_are_not_invented(self):
        metrics = parse_trace([
            "LAB_TRACE t_us=0 event=fetch_admitted gen=1 focus=0 msg=- "
            "trace_schema=lab-trace/v1 run_id=r session_id=1 op_kind=http_fetch op_id=7 attempt=1\n",
            "LAB_TRACE t_us=1 event=publication gen=1 focus=0 msg=msg-1 "
            "trace_schema=lab-trace/v1 run_id=r session_id=1 card_slot=0 "
            "revision=2 publication_after_apply=true draw_buf_bytes=32000\n",
            "LAB_TRACE t_us=2 event=session_reset gen=0 focus=0 msg=- "
            "trace_schema=lab-trace/v1 session_id=2\n",
        ])
        self.assertEqual(metrics["metadata"]["effective_config"]["draw_buf_bytes"], 32000)
        self.assertIn("session_reset_violation", metrics["evidence"]["invalid"])
        self.assertEqual(metrics["evidence"]["status"], "INCOMPLETE")

    def test_duplicate_structured_field_is_malformed(self):
        metrics = parse_trace([
            "LAB_TRACE t_us=1 event=bind gen=1 focus=0 msg=- "
            "trace_schema=lab-trace/v1 session_id=1 session_id=2\n",
        ])
        self.assertEqual(metrics["event_counts"], {})
        self.assertEqual(metrics["counts"]["malformed_trace"], 1)
        self.assertEqual(metrics["trace_loss"]["status"], "OBSERVED")

    def test_present_latency_fields_are_measured_without_pooling_failure(self):
        metrics = parse_trace([
            "LAB_TRACE t_us=100 event=ui_tick gen=0 focus=0 msg=- session_id=1\n",
            "LAB_TRACE t_us=300 event=ui_tick gen=0 focus=0 msg=- session_id=1\n",
            "LAB_TRACE t_us=400 event=bind gen=1 focus=0 msg=m1 elapsed_us=150\n",
            "LAB_TRACE t_us=500 event=input_to_visible gen=1 focus=0 msg=m1 elapsed_us=800\n",
            "LAB_TRACE t_us=1000 event=fetch_admitted gen=1 focus=0 msg=- "
            "run_id=r session_id=1 op_kind=http_fetch op_id=4 attempt=1 "
            "submitted_us=1000 started_us=1200\n",
            "LAB_TRACE t_us=1400 event=fetch_terminal gen=1 focus=0 msg=- "
            "run_id=r session_id=1 op_kind=http_fetch op_id=4 attempt=1 "
            "elapsed_us=200 outcome=failure\n",
            "LAB_TRACE_LOSS status=NONE dropped_events=0 oversized_events=0 malformed_rows=0\n",
        ])
        self.assertEqual(metrics["distributions"]["ui_tick_ms"]["p50"], 0.2)
        self.assertEqual(metrics["distributions"]["bind_ms"]["worst"], 0.15)
        self.assertEqual(metrics["distributions"]["input_to_visible_ms"]["worst"], 0.8)
        self.assertEqual(metrics["distributions"]["fetch_queue_wait_ms"]["worst"], 0.2)
        self.assertEqual(metrics["distributions"]["fetch_attempt_ms"]["n"], 0)
        self.assertEqual(metrics["distribution_outcomes"]["fetch_attempt_ms"]["failure"]["n"], 1)

    def test_observed_loss_status_is_authoritative_when_counts_are_zero(self):
        metrics = parse_trace([
            "LAB_TRACE_LOSS status=OBSERVED dropped_events=0 oversized_events=0 malformed_rows=0\n",
        ])
        self.assertEqual(metrics["trace_loss"], {
            "status": "OBSERVED", "dropped_events": 0,
            "oversized_events": 0, "malformed_rows": 0,
        })
        self.assertEqual(metrics["counts"]["trace_dropped"], 0)
        self.assertEqual(metrics["evidence"]["status"], "INCOMPLETE")

    def test_fetch_queue_wait_requires_complete_fetch_identity(self):
        metrics = parse_trace([
            "LAB_TRACE t_us=1 event=fetch_admitted gen=1 focus=0 msg=- "
            "session_id=1 submitted_us=100 started_us=500\n",
            "LAB_TRACE t_us=2 event=fetch_terminal gen=1 focus=0 msg=- "
            "session_id=1 submitted_us=100 started_us=500 elapsed_us=400 outcome=success\n",
        ])
        self.assertIsNone(metrics["distributions"]["fetch_queue_wait_ms"])

    def test_trace_build_id_is_retained_and_ready_build_remains_supported(self):
        trace_build = parse_trace([
            "LAB_TRACE t_us=1 event=bind gen=1 focus=0 msg=- "
            "trace_schema=lab-trace/v1 build_id=trace-build\n",
        ])
        ready_build = parse_trace([
            "LAB02_READY build=ready-build fixture=inbox_8 mode=recycle\n",
        ])
        self.assertEqual(trace_build["metadata"]["build_id"], "trace-build")
        self.assertEqual(ready_build["metadata"]["build_id"], "ready-build")

    def test_session_reset_invalidates_fetch_and_snap_timing_samples(self):
        metrics = parse_trace([
            "LAB_TRACE t_us=0 event=fetch_admitted gen=1 focus=0 msg=- "
            "run_id=r session_id=1 op_kind=http_fetch op_id=8 attempt=1 "
            "submitted_us=0 started_us=10\n",
            "LAB_TRACE t_us=1 event=snap_start gen=1 focus=0 msg=- "
            "trace_schema=lab-trace/v1 run_id=r session_id=1 snap_id=2\n",
            "LAB_TRACE t_us=5 event=session_reset gen=0 focus=0 msg=- session_id=2\n",
            "LAB_TRACE t_us=20 event=fetch_terminal gen=1 focus=0 msg=- "
            "run_id=r session_id=1 op_kind=http_fetch op_id=8 attempt=1 "
            "elapsed_us=900 outcome=success\n",
            "LAB_TRACE t_us=21 event=snap_end gen=1 focus=0 msg=- "
            "trace_schema=lab-trace/v1 run_id=r session_id=1 snap_id=2 "
            "elapsed_us=700 outcome=success\n",
        ])
        self.assertEqual(metrics["distributions"]["fetch_attempt_ms"]["n"], 0)
        self.assertIsNone(metrics["distributions"]["fetch_queue_wait_ms"])
        self.assertEqual(metrics["distributions"]["snap_ms"]["n"], 0)
        self.assertIn("session_reset_violation", metrics["evidence"]["invalid"])

    def test_duplicate_admission_invalidates_fetch_and_snap_timing_samples(self):
        metrics = parse_trace([
            "LAB_TRACE t_us=0 event=fetch_admitted gen=1 focus=0 msg=- "
            "run_id=r session_id=1 op_kind=http_fetch op_id=9 attempt=1 "
            "submitted_us=0 started_us=10\n",
            "LAB_TRACE t_us=1 event=fetch_admitted gen=1 focus=0 msg=- "
            "run_id=r session_id=1 op_kind=http_fetch op_id=9 attempt=1 "
            "submitted_us=0 started_us=10\n",
            "LAB_TRACE t_us=2 event=fetch_terminal gen=1 focus=0 msg=- "
            "run_id=r session_id=1 op_kind=http_fetch op_id=9 attempt=1 "
            "elapsed_us=900 outcome=success\n",
            "LAB_TRACE t_us=3 event=snap_start gen=1 focus=0 msg=- "
            "run_id=r session_id=1 snap_id=3\n",
            "LAB_TRACE t_us=4 event=snap_start gen=1 focus=0 msg=- "
            "run_id=r session_id=1 snap_id=3\n",
            "LAB_TRACE t_us=5 event=snap_end gen=1 focus=0 msg=- "
            "run_id=r session_id=1 snap_id=3 elapsed_us=800 outcome=success\n",
        ])
        self.assertEqual(metrics["distributions"]["fetch_attempt_ms"]["n"], 0)
        self.assertIsNone(metrics["distributions"]["fetch_queue_wait_ms"])
        self.assertEqual(metrics["distributions"]["snap_ms"]["n"], 0)
        self.assertIn("duplicate_start:fetch", metrics["evidence"]["invalid"])
        self.assertIn("duplicate_start:snap", metrics["evidence"]["invalid"])

    def test_loss_observed_cannot_be_downgraded_by_later_none_marker(self):
        metrics = parse_trace([
            "LAB_TRACE_LOSS status=OBSERVED dropped_events=0 oversized_events=0 malformed_rows=0\n",
            "LAB_TRACE_LOSS status=NONE dropped_events=0 oversized_events=0 malformed_rows=0\n",
        ])
        self.assertEqual(metrics["trace_loss"]["status"], "OBSERVED")
        self.assertEqual(metrics["evidence"]["status"], "INCOMPLETE")


if __name__ == "__main__":
    unittest.main()
