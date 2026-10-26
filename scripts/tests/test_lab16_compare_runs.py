"""LAB-16 upload metrics pair comparison."""
from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
COMPARE = ROOT / "client/labs/lab-16-attached-storage/tools/compare_upload_runs.py"
spec = importlib.util.spec_from_file_location("lab16_compare_upload_runs", COMPARE)
assert spec is not None and spec.loader is not None
compare = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = compare
spec.loader.exec_module(compare)


def _window(beat: str, **overrides: int) -> dict:
    base = {
        "internal_min_free": 260000,
        "internal_min_largest": 170000,
        "dma_min_free": 250000,
        "dma_min_largest": 170000,
        "psram_min_free": 16_000_000,
        "psram_min_largest": 8_000_000,
        "main_task_stack_hwm_sdk_units": 10000,
        "main_task_stack_type_bytes": 1,
        "sampler_task_stack_hwm_sdk_units": 512,
        "sampler_task_stack_type_bytes": 1,
        "event_loop_task_stack_hwm_sdk_units": 256,
        "event_loop_task_stack_type_bytes": 1,
        "wifi_task_stack_hwm_sdk_units": 2048,
        "wifi_task_stack_type_bytes": 1,
        "tcpip_task_stack_hwm_sdk_units": 512,
        "tcpip_task_stack_type_bytes": 1,
        "pcm_bytes_max": 0,
        "sd_driver_writes_max": 0,
        "outstanding_buffer_count_max": 0,
        "outstanding_buffer_bytes_max": 0,
        "request_inflight_max": 0,
        "accepted_requests_max": 0,
        "failed_requests_max": 0,
        "retries_max": 0,
    }
    base.update(overrides)
    return base


def _metrics(**beat_overrides) -> dict:
    beats = [
        "baseline_before_sd",
        "sd_mount_probe",
        "mic_i2s_open_start",
        "mic_i2s_open_complete",
        "sd_mic_recording",
        "wifi_upload_active",
        "upload_complete_workers_quiescent",
    ]
    windows = {beat: _window(beat, **beat_overrides.get(beat, {})) for beat in beats}
    return {
        "parser": "lab16-upload-evidence-parser/v3",
        "variant": "upload",
        "memory_status": "COMPLETE",
        "functional_status": "PASS",
        "observed_beats": beats,
        "beat_windows": windows,
        "expected_chunks": 210,
        "expected_pcm_bytes": 6_720_000,
        "recorded_pcm_bytes": 6_720_000,
        "device_chunk_source_count": 210,
        "device_chunk_terminal_count": 210,
        "server_chunk_count": 210,
        "stream_elapsed_ms": 210_000,
        "request_latencies_us": [100_000] * 210,
    }


class Lab16CompareRunsTest(unittest.TestCase):
    def test_identical_metrics_pass(self):
        a = _metrics()
        b = _metrics()
        report = compare.compare_upload_metrics(a, b, run_a="one", run_b="two")
        self.assertTrue(report.repeatability_pass)
        self.assertEqual(report.memory_checks_passed, report.memory_checks_total)

    def test_memory_failure_detected(self):
        a = _metrics()
        b = _metrics()
        b["beat_windows"]["wifi_upload_active"]["internal_min_free"] = 100_000
        report = compare.compare_upload_metrics(a, b, run_a="one", run_b="two")
        self.assertFalse(report.repeatability_pass)
        self.assertTrue(report.memory_failures)

    def test_manual_pair_integration(self):
        run_a = ROOT / "docs/labs/evidence/lab-16/runs/20261025-upload-manual-01/metrics.json"
        run_b = ROOT / "docs/labs/evidence/lab-16/runs/20261025-upload-manual-02/metrics.json"
        if not run_a.is_file() or not run_b.is_file():
            self.skipTest("manual run metrics not present in workspace")
        report = compare.compare_run_files(run_a, run_b, run_a="manual-01", run_b="manual-02")
        self.assertTrue(report.repeatability_pass)


if __name__ == "__main__":
    unittest.main()
