from __future__ import annotations

import hashlib
import importlib.util
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))

import lab_adapter_lab16 as adapter
from lab_run_evidence import EvidenceRun

PARSER_SOURCE = ROOT / "client/labs/lab-16-attached-storage/tools/parse_upload_evidence.py"


def _one_memory_window(parser_path: Path) -> str:
    spec = importlib.util.spec_from_file_location("test_lab16_upload_parser", parser_path)
    assert spec is not None and spec.loader is not None
    parser = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(parser)
    fields: dict[str, str] = {
        "schema": "lab16-memory/v3", "event": "window", "seq": "1",
        "sample_interval_ms": "50", "sample_count": "1", "t_start_us": "100",
        "t_end_us": "150", "beat": "baseline_before_sd",
    }
    values = {
        "internal_total": 1000, "internal_interval_min_free": 800,
        "internal_interval_min_free_t_us": 120, "internal_interval_min_largest": 400,
        "internal_interval_min_largest_t_us": 120,
        "internal_cumulative_min_free_since_heap_init": 800,
        "internal_cumulative_min_free_t_us": 120,
        "internal_cumulative_min_free_delta_from_baseline": 0,
        "internal_current_free": 800, "internal_current_largest": 400,
        "dma_total": 1000, "dma_interval_min_free": 800, "dma_interval_min_free_t_us": 120,
        "dma_interval_min_largest": 400, "dma_interval_min_largest_t_us": 120,
        "dma_cumulative_min_free_since_heap_init": 800, "dma_cumulative_min_free_t_us": 120,
        "dma_cumulative_min_free_delta_from_baseline": 0, "dma_current_free": 800,
        "dma_current_largest": 400, "psram_total": 1000, "psram_interval_min_free": 800,
        "psram_interval_min_free_t_us": 120, "psram_interval_min_largest": 400,
        "psram_interval_min_largest_t_us": 120,
        "psram_cumulative_min_free_since_heap_init": 800,
        "psram_cumulative_min_free_t_us": 120,
        "psram_cumulative_min_free_delta_from_baseline": 0,
        "psram_current_free": 800, "psram_current_largest": 400,
        "pcm_bytes_max": 0, "sd_driver_writes_max": 0,
        "outstanding_buffer_count_max": 0, "outstanding_buffer_bytes_max": 0,
        "request_inflight_max": 0, "accepted_requests_max": 0,
        "failed_requests_max": 0, "retries_max": 0,
    }
    fields.update({key: str(value) for key, value in values.items()})
    for task, (name, configured, allocated) in parser._STACK_TASKS.items():
        fields.update({
            f"{task}_name": name,
            f"{task}_configured_stack_bytes": str(configured),
            f"{task}_allocated_stack_bytes": str(allocated),
            f"{task}_status": "captured",
            f"{task}_stack_hwm_sdk_units": "128",
            f"{task}_stack_hwm_unit": "StackType_t_items",
            f"{task}_stack_type_bytes": "1",
        })
    return "LAB16_MEM " + " ".join(f"{key}={value}" for key, value in fields.items())


class Lab16UploadPartialCaptureTest(unittest.TestCase):
    def _run_interrupted_capture(self, serial_contents: str, run_id: str) -> tuple[dict, Path]:
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        repo_root = Path(temp.name)
        parser_path = repo_root / adapter.UPLOAD_PARSER
        parser_path.parent.mkdir(parents=True)
        shutil.copyfile(PARSER_SOURCE, parser_path)

        evidence = EvidenceRun(repo_root, run_id, {}, lab_id="lab-16", evidence_slug="lab-16")
        serial_path = evidence.serial_root / run_id / "serial.txt"
        serial_path.parent.mkdir(parents=True)
        server_log = evidence.run_dir / "server.log"
        server_log.write_text("partial server capture\n", encoding="utf-8", newline="\n")
        evidence.update_context({
            "artifacts": {"serial_log": serial_path.relative_to(repo_root).as_posix(),
                          "server_log": server_log.relative_to(repo_root).as_posix()},
            "capture": {"status": "capturing"},
        })
        identity = {
            "fixture_id": "default-upload", "fixture_revision": "2",
            "fixture_schema": "lab16-upload/v2", "fixture_path": "fixtures/upload/fixture.json",
            "fixture_sha256": "a" * 64, "generator_version": "test-fixture-generator/v2",
            "seed": "test-seed", "path": "server/labs/lab-16-attached-storage",
            "revision": "lab16-v1", "source_sha256": "b" * 64,
        }

        def interrupt(*_args, **_kwargs):
            serial_path.write_text(serial_contents, encoding="utf-8", newline="\n")
            raise OSError("simulated serial read interruption")

        with patch.object(adapter, "capture_serial", side_effect=interrupt):
            with self.assertRaisesRegex(OSError, "simulated serial read interruption"):
                adapter.capture_serial_with_partial_upload_evidence(
                    capture_args=(), capture_kwargs={}, selected_variant="upload", repo_root=repo_root,
                    evidence=evidence, serial_path=serial_path, server=adapter.OwnedServer(),
                    device_token="test-token", identity=identity, run_id=run_id,
                )
        manifest = evidence.finalize(workflow_status="INCOMPLETE", experiment_verdict="UNMEASURED",
                                     caller_completed=False, cleanup_succeeded=True,
                                     terminal_reason="serial_capture_interrupted")
        return manifest, repo_root

    def test_interruption_preserves_metrics_and_hash_in_partial_manifest(self):
        # The parser is copied under the temporary repo by the helper; use a
        # valid v3 sample followed by a torn record to exercise prefix salvage.
        with tempfile.TemporaryDirectory() as sample_root:
            sample_parser = Path(sample_root) / "parse_upload_evidence.py"
            shutil.copyfile(PARSER_SOURCE, sample_parser)
            serial = _one_memory_window(sample_parser) + "\nLAB16_MEM schema=lab16-memory/v3 event=window seq=\n"
        manifest, repo_root = self._run_interrupted_capture(serial, "partial-with-sample")
        metrics_rel = manifest["artifacts"]["metrics"]
        self.assertEqual(metrics_rel, "docs/labs/evidence/lab-16/runs/partial-with-sample/metrics.json")
        metrics_path = repo_root / Path(*metrics_rel.split("/"))
        metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
        self.assertEqual(metrics["memory_status"], "INCOMPLETE")
        self.assertEqual(len(metrics["memory_samples"]), 1)
        self.assertEqual(manifest["capture"]["metrics_sha256"], hashlib.sha256(metrics_path.read_bytes()).hexdigest())
        self.assertEqual(manifest["workflow_status"], "INCOMPLETE")

    def test_interruption_without_memory_samples_does_not_invent_metrics(self):
        manifest, repo_root = self._run_interrupted_capture("LAB16_MEM schema=lab16-memory/v3 event=window seq=\n",
                                                            "partial-without-sample")
        self.assertIsNone(manifest["artifacts"]["metrics"])
        self.assertFalse((repo_root / "docs/labs/evidence/lab-16/runs/partial-without-sample/metrics.json").exists())


if __name__ == "__main__":
    unittest.main()
