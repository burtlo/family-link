import json
import subprocess
import tempfile
import threading
import unittest
from pathlib import Path

from lab02_evidence import EvidenceRun


def _link_directory(link: Path, target: Path) -> None:
    """Link a directory so path resolution escapes the approved root.

    A directory symlink is preferred. When SeCreateSymbolicLinkPrivilege is
    absent, a junction still resolves outside the root and the escape check
    remains in force.
    """
    try:
        link.symlink_to(target, target_is_directory=True)
    except OSError as exc:
        if getattr(exc, "winerror", None) != 1314:
            raise
        completed = subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(link), str(target)],
            capture_output=True,
            text=True,
            check=False,
        )
        if completed.returncode != 0:
            detail = (completed.stderr or completed.stdout).strip()
            raise unittest.SkipTest(
                "SeCreateSymbolicLinkPrivilege is absent and a directory junction "
                f"could not be created: {detail}"
            ) from exc


def _unlink_directory_link(link: Path) -> None:
    if link.is_symlink() or link.exists():
        link.unlink()


class EvidenceRunTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def run_writer(self, run_id, context=None, **kwargs):
        return EvidenceRun(self.root, run_id, context or {}, **kwargs)

    def test_complete_run_records_truthful_summary_and_is_idempotent(self):
        run = self.run_writer("20261012-phaseb-64-nominal-01", {
            "scenarios": ["nominal"], "operator": "harness",
            "firmware": {"build_id": "stamp-1", "cmake_options": {
                "LAB02_FIXTURE": "64", "WIFI_PASSWORD": "hidden"}},
            "artifacts": {"serial_log": "logs/device/labs/lab-02/run/serial.txt",
                          "metrics": None},
        })
        run.event_sink({"kind": "stage", "stage": "capture", "status": "complete",
                        "url": "http://user:password@host/path", "wifi_password": "secret"})
        first = run.finalize(workflow_status="PASS", experiment_verdict="UNMEASURED",
                             caller_completed=True, cleanup_succeeded=True)
        second = run.finalize(workflow_status="FAIL", experiment_verdict="FAIL",
                              caller_completed=False, cleanup_succeeded=False)
        self.assertEqual(first, second)
        manifest = json.loads(run.manifest_path.read_text())
        self.assertEqual(manifest["document"], "family-link.lab-run-manifest/v1")
        self.assertEqual(manifest["workflow_status"], "PASS")
        self.assertEqual(manifest["experiment_verdict"], "UNMEASURED")
        self.assertEqual(manifest["firmware"]["cmake_options"], {"LAB02_FIXTURE": "64"})
        self.assertIsNone(manifest["firmware"]["binary_sha256"])
        self.assertEqual(manifest["event_loss"]["host_journal"],
                         {"status": "NONE", "dropped_events": 0})
        self.assertEqual(manifest["event_loss"]["firmware_trace"],
                         {"status": "UNKNOWN", "dropped_events": None})
        events = run.events_path.read_text()
        self.assertTrue(all(row.get("source") in {"host", "server", "device", "unknown"}
                            for row in map(json.loads, events.splitlines())))
        self.assertNotIn("secret", events)
        self.assertNotIn("user:password", events)
        self.assertEqual(events.count('"kind":"run_terminal"'), 1)
        with self.assertRaises(FileExistsError):
            self.run_writer("20261012-phaseb-64-nominal-01")

    def test_preflight_and_build_failure_can_finalize_without_serial_or_metrics(self):
        for rid, stage in (("preflight", "preflight"), ("build-failure", "build")):
            run = self.run_writer(rid, {"build": {"status": "failed" if stage == "build" else "not_started"}})
            run.event_sink({"kind": "stage", "stage": stage, "status": "failed"})
            result = run.finalize(workflow_status="FAIL", experiment_verdict="INCOMPLETE",
                                  caller_completed=False, cleanup_succeeded=True,
                                  terminal_reason=f"{stage} rejected")
            self.assertIsNone(result["firmware"]["build_id"])
            self.assertIsNone(result["artifacts"]["serial_log"])
            self.assertIsNone(result["artifacts"]["metrics"])

    def test_interrupt_cleanup_failure_and_semantic_failure_are_distinct(self):
        interrupted = self.run_writer("ctrl-c")
        interrupted.event_sink({"kind": "interrupted", "signal": "SIGINT"})
        a = interrupted.finalize(workflow_status="INCOMPLETE", experiment_verdict="INCOMPLETE",
                                 caller_completed=False, cleanup_succeeded=True)
        self.assertEqual(a["workflow_status"], "INCOMPLETE")
        failed_cleanup = self.run_writer("cleanup-failure")
        failed_cleanup.event_sink({"kind": "cleanup", "status": "failed", "reason": "child stuck"})
        b = failed_cleanup.finalize(workflow_status="FAIL", experiment_verdict="UNMEASURED",
                                    caller_completed=False, cleanup_succeeded=False)
        self.assertEqual(b["workflow_status"], "FAIL")
        semantic = self.run_writer("semantic-failure")
        c = semantic.finalize(workflow_status="PASS", experiment_verdict="FAIL",
                              caller_completed=True, cleanup_succeeded=True)
        self.assertEqual((c["workflow_status"], c["experiment_verdict"]), ("PASS", "FAIL"))

    def test_safe_id_status_validation_and_threaded_event_sink(self):
        for unsafe in ("../escape", "/absolute", "a/b", ""):
            with self.assertRaises(ValueError):
                self.run_writer(unsafe)
        run = self.run_writer("threaded", max_events=100)
        threads = [threading.Thread(target=lambda n=n: run.event_sink({"kind": "tick", "n": n}))
                   for n in range(12)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        with self.assertRaises(ValueError):
            run.finalize(workflow_status="PASS", experiment_verdict="PASS",
                         caller_completed=False, cleanup_succeeded=True)
        run.finalize(workflow_status="FAIL", experiment_verdict="UNMEASURED",
                     caller_completed=False, cleanup_succeeded=True)
        rows = [json.loads(line) for line in run.events_path.read_text().splitlines()]
        self.assertEqual(sum(row.get("kind") == "run_terminal" for row in rows), 1)
        self.assertEqual(sum(row.get("kind") == "tick" for row in rows), 12)

    def test_event_quota_bounds_memory_and_jsonl_and_reserves_terminal(self):
        run = self.run_writer("quota", max_events=2)
        threads = [threading.Thread(target=lambda n=n: run.event_sink({"kind": "tick", "n": n}))
                   for n in range(20)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        run.event_sink({"kind": "large", "payload": "x" * 20000})
        result = run.finalize(workflow_status="FAIL", experiment_verdict="UNMEASURED",
                              caller_completed=False, cleanup_succeeded=True)
        rows = [json.loads(line) for line in run.events_path.read_text().splitlines()]
        self.assertLessEqual(len(run._events), 4)  # quota + marker + terminal
        self.assertLessEqual(len(rows), 4)
        self.assertEqual(sum(row.get("kind") == "events_truncated" for row in rows), 1)
        self.assertEqual(sum(row.get("kind") == "run_terminal" for row in rows), 1)
        self.assertEqual(result["event_loss"]["dropped_events"], 19)
        self.assertEqual(result["event_loss"]["host_journal"],
                         {"status": "OBSERVED", "dropped_events": 19})

    def test_loss_sources_are_separate_and_unknown_is_not_zero(self):
        run = self.run_writer("loss-sources")
        run.event_sink({"source": "device", "kind": "device_trace", "event": "snap_start"})
        run.update_context({"event_loss": {"firmware_trace": {
            "status": "OBSERVED", "dropped_events": 3,
            "oversized_events": 1, "malformed_rows": 0}}})
        result = run.finalize(workflow_status="PASS", experiment_verdict="UNMEASURED",
                              caller_completed=True, cleanup_succeeded=True)
        self.assertEqual(result["event_loss"]["host_journal"],
                         {"status": "NONE", "dropped_events": 0})
        self.assertEqual(result["event_loss"]["firmware_trace"]["dropped_events"], 3)
        row = json.loads(run.events_path.read_text().splitlines()[0])
        self.assertEqual(row["source"], "device")

    def test_invalid_or_missing_event_source_is_explicit_unknown(self):
        run = self.run_writer("unknown-source")
        run.event_sink({"kind": "legacy_event"})
        run.event_sink({"source": "other", "kind": "bad_source"})
        rows = [json.loads(row) for row in run.events_path.read_text().splitlines()]
        self.assertEqual([row["source"] for row in rows], ["unknown", "unknown"])

    def test_atomic_manifest_failure_retries_same_terminal_facts(self):
        import lab02_evidence as module
        run = self.run_writer("retry")
        original = module._atomic_json
        calls = 0
        def fail_once(path, value):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise OSError("simulated write failure")
            return original(path, value)
        module._atomic_json = fail_once
        try:
            with self.assertRaises(OSError):
                run.finalize(workflow_status="FAIL", experiment_verdict="UNMEASURED",
                              caller_completed=False, cleanup_succeeded=True,
                              terminal_reason="first")
            result = run.finalize(workflow_status="PASS", experiment_verdict="PASS",
                                  caller_completed=True, cleanup_succeeded=True,
                                  terminal_reason="changed")
        finally:
            module._atomic_json = original
        rows = [json.loads(line) for line in run.events_path.read_text().splitlines()]
        terminal = [row for row in rows if row.get("kind") == "run_terminal"]
        self.assertEqual(len(terminal), 1)
        self.assertEqual(terminal[0]["workflow_status"], "FAIL")
        self.assertEqual(result["workflow_status"], "FAIL")
        self.assertEqual(json.loads(run.manifest_path.read_text()), result)
        self.assertEqual(list(run.run_dir.glob("*.tmp")), [])

    def test_terminal_reason_is_sanitized_and_encoded_row_bounded(self):
        for rid, reason in (
            ("long-ascii", "x" * 20000),
            ("long-escaped", ('"\\\n\t' * 5000)),
            ("long-unicode", "\U0001f4a9" * 5000),
            ("long-redacted", ("token=hidden " + "z" * 20000)),
        ):
            with self.subTest(run_id=rid):
                run = self.run_writer(rid)
                result = run.finalize(workflow_status="FAIL", experiment_verdict="UNMEASURED",
                                      caller_completed=False, cleanup_succeeded=True,
                                      terminal_reason=reason)
                rows = [json.loads(line) for line in run.events_path.read_text().splitlines()]
                terminal = next(row for row in rows if row.get("kind") == "run_terminal")
                self.assertLessEqual(len(json.dumps(terminal, sort_keys=True,
                                                     separators=(",", ":")).encode("utf-8")),
                                     run.max_event_bytes)
                self.assertTrue(result["terminal_reason_truncated"])
                self.assertTrue(terminal["reason_truncated"])
                self.assertEqual(result["terminal_reason"], terminal["reason"])
                self.assertTrue(terminal["reason"].endswith("[truncated]"))
                self.assertNotIn("hidden", run.events_path.read_text())

    def test_oversized_terminal_reason_retry_keeps_one_bounded_frozen_event(self):
        import lab02_evidence as module
        run = self.run_writer("bounded-retry")
        original = module._atomic_json
        calls = 0
        def fail_once(path, value):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise OSError("simulated write failure")
            return original(path, value)
        module._atomic_json = fail_once
        try:
            with self.assertRaises(OSError):
                run.finalize(workflow_status="FAIL", experiment_verdict="UNMEASURED",
                              caller_completed=False, cleanup_succeeded=True,
                              terminal_reason="a" * 20000)
            result = run.finalize(workflow_status="PASS", experiment_verdict="PASS",
                                  caller_completed=True, cleanup_succeeded=True,
                                  terminal_reason="changed")
        finally:
            module._atomic_json = original
        rows = [json.loads(line) for line in run.events_path.read_text().splitlines()]
        terminal = [row for row in rows if row.get("kind") == "run_terminal"]
        self.assertEqual(len(terminal), 1)
        self.assertEqual(terminal[0]["reason"], result["terminal_reason"])
        self.assertEqual(terminal[0]["workflow_status"], "FAIL")
        self.assertTrue(result["terminal_reason_truncated"])
        self.assertLessEqual(len(json.dumps(terminal[0], sort_keys=True,
                                            separators=(",", ":")).encode("utf-8")),
                             run.max_event_bytes)

    def test_artifact_paths_must_be_canonical_existing_files_inside_root(self):
        run = self.run_writer("artifact", {"artifacts": {
            "serial_log": "private/local.yaml", "metrics": "unrelated.txt"}})
        result = run.finalize(workflow_status="FAIL", experiment_verdict="UNMEASURED",
                              caller_completed=False, cleanup_succeeded=True)
        self.assertIsNone(result["artifacts"]["serial_log"])
        self.assertIsNone(result["artifacts"]["metrics"])
        log_path = self.root / "logs/device/labs/lab-02/artifact/serial.txt"
        log_path.parent.mkdir(parents=True)
        log_path.write_text("captured")
        # A later run may reference only its own canonical capture file.
        valid = self.run_writer("valid-artifact", {"artifacts": {
            "serial_log": "logs/device/labs/lab-02/valid-artifact/serial.txt"}})
        valid_log = self.root / "logs/device/labs/lab-02/valid-artifact/serial.txt"
        valid_log.parent.mkdir(parents=True)
        valid_log.write_text("captured")
        result = valid.finalize(workflow_status="FAIL", experiment_verdict="UNMEASURED",
                                 caller_completed=False, cleanup_succeeded=True)
        self.assertEqual(result["artifacts"]["serial_log"],
                         "logs/device/labs/lab-02/valid-artifact/serial.txt")
        outside = Path(self.temp.name).parent / (Path(self.temp.name).name + "-outside")
        outside.mkdir()
        link = self.root / "logs/device/labs/lab-02" / "symlink-escape"
        try:
            escape = self.run_writer("symlink-escape", {"artifacts": {
                "serial_log": "logs/device/labs/lab-02/symlink-escape/serial.txt"}})
            link.parent.mkdir(parents=True, exist_ok=True)
            _link_directory(link, outside)
            (outside / "serial.txt").write_text("outside")
            result = escape.finalize(workflow_status="FAIL", experiment_verdict="UNMEASURED",
                                     caller_completed=False, cleanup_succeeded=True)
            self.assertIsNone(result["artifacts"]["serial_log"])
        finally:
            import shutil
            _unlink_directory_link(link)
            shutil.rmtree(outside, ignore_errors=True)

    def test_context_updates_merge_observed_partial_facts_safely(self):
        run = self.run_writer("partial", {"build": {"status": "not_started"},
                                           "firmware": {"binary_sha256": "observed-hash"}})
        run.update_context({"firmware": {"build_id": "observed-1"},
                            "hardware": {"kit_nickname": "kit-a"},
                            "unrecognized": object()})
        result = run.finalize(workflow_status="FAIL", experiment_verdict="INCOMPLETE",
                              caller_completed=False, cleanup_succeeded=True)
        self.assertEqual(result["firmware"]["build_id"], "observed-1")
        self.assertEqual(result["hardware"]["kit_nickname"], "kit-a")
        self.assertEqual(result["firmware"]["binary_sha256"], "observed-hash")
        with self.assertRaises(RuntimeError):
            run.update_context({"firmware": {"build_id": "late"}})

    def test_generic_manifest_uses_registered_identity_and_approved_run_roots(self):
        run_id = "transition-run-01"
        run = EvidenceRun(
            self.root, run_id,
            {"firmware": {"build_fingerprint": "sha256:compiled-inputs"}},
            lab_id="LAB-05", evidence_slug="transition-resource",
            evidence_root="docs/labs/evidence/transition-resource/runs",
            serial_root="logs/device/labs/transition-resource",
            document="family-link.lab-run-manifest/v2")
        metrics = self.root / "docs/labs/evidence/transition-resource/runs" / run_id / "metrics.json"
        serial = self.root / "logs/device/labs/transition-resource" / run_id / "serial.txt"
        metrics.write_text('{"document":"family-link.lab-metrics/v2"}')
        serial.parent.mkdir(parents=True)
        serial.write_text("captured")
        run.update_context({"artifacts": {
            "metrics": metrics.relative_to(self.root).as_posix(),
            "serial_log": serial.relative_to(self.root).as_posix()}})
        result = run.finalize(workflow_status="PASS", experiment_verdict="FAIL",
                              caller_completed=True, cleanup_succeeded=True)
        self.assertEqual(result["document"], "family-link.lab-run-manifest/v2")
        self.assertEqual(result["lab_id"], "LAB-05")
        self.assertEqual(result["evidence_slug"], "transition-resource")
        self.assertEqual(result["firmware"]["build_fingerprint"], "sha256:compiled-inputs")
        self.assertEqual(result["run_id"], run_id)
        self.assertEqual(result["workflow_status"], "PASS")
        self.assertEqual(result["experiment_verdict"], "FAIL")
        self.assertEqual(result["artifacts"]["metrics"], metrics.relative_to(self.root).as_posix())
        self.assertEqual(result["artifacts"]["serial_log"], serial.relative_to(self.root).as_posix())

    def test_v1_rejects_custom_roots_while_v2_accepts_them(self):
        with self.assertRaisesRegex(ValueError, "v1 manifest requires the canonical evidence_root"):
            self.run_writer("v1-custom-root", evidence_root="custom/evidence/runs")
        with self.assertRaisesRegex(ValueError, "v1 manifest requires the canonical serial_root"):
            self.run_writer("v1-custom-serial-root", serial_root="custom/device-logs")

        run_id = "v2-custom-root"
        evidence_root = "custom/evidence/runs"
        serial_root = "custom/device-logs"
        run = EvidenceRun(
            self.root, run_id, {}, lab_id="LAB-05", evidence_slug="transition-resource",
            evidence_root=evidence_root, serial_root=serial_root,
            document="family-link.lab-run-manifest/v2")
        metrics = self.root / evidence_root / run_id / "metrics.json"
        serial = self.root / serial_root / run_id / "serial.txt"
        metrics.write_text('{"document":"family-link.lab-metrics/v2"}')
        serial.parent.mkdir(parents=True)
        serial.write_text("captured")
        run.update_context({"artifacts": {
            "metrics": metrics.relative_to(self.root).as_posix(),
            "serial_log": serial.relative_to(self.root).as_posix()}})
        result = run.finalize(workflow_status="FAIL", experiment_verdict="UNMEASURED",
                              caller_completed=False, cleanup_succeeded=True)
        self.assertEqual(result["document"], "family-link.lab-run-manifest/v2")
        self.assertEqual(result["artifacts"]["metrics"], metrics.relative_to(self.root).as_posix())
        self.assertEqual(result["artifacts"]["serial_log"], serial.relative_to(self.root).as_posix())

    def test_generic_paths_reject_traversal_unapproved_and_symlink_escape(self):
        with self.assertRaises(ValueError):
            self.run_writer("bad-root", evidence_root="../outside",
                            document="family-link.lab-run-manifest/v2",
                            lab_id="LAB-05", evidence_slug="transition-resource")
        run_id = "generic-unsafe"
        root = "docs/labs/evidence/transition-resource/runs"
        run = EvidenceRun(self.root, run_id, {"artifacts": {
            "metrics": f"{root}/{run_id}/../other/metrics.json",
            "serial_log": f"logs/device/labs/other/{run_id}/serial.txt"}},
            lab_id="LAB-05", evidence_slug="transition-resource",
            evidence_root=root,
            serial_root="logs/device/labs/transition-resource",
            document="family-link.lab-run-manifest/v2")
        outside = Path(self.temp.name).parent / (Path(self.temp.name).name + "-generic-outside")
        outside.mkdir()
        serial_dir = self.root / "logs/device/labs/transition-resource" / run_id
        try:
            serial_dir.parent.mkdir(parents=True, exist_ok=True)
            _link_directory(serial_dir, outside)
            (outside / "serial.txt").write_text("outside")
            result = run.finalize(workflow_status="FAIL", experiment_verdict="INCOMPLETE",
                                  caller_completed=False, cleanup_succeeded=False)
            self.assertIsNone(result["artifacts"]["metrics"])
            self.assertIsNone(result["artifacts"]["serial_log"])
            self.assertEqual(result["workflow_status"], "FAIL")
            self.assertEqual(result["experiment_verdict"], "INCOMPLETE")
            self.assertEqual(result["event_loss"]["dropped_events"], 0)
        finally:
            import shutil
            _unlink_directory_link(serial_dir)
            shutil.rmtree(outside, ignore_errors=True)

    def test_generic_failure_finalizes_partial_truthful_evidence(self):
        run = EvidenceRun(self.root, "generic-preflight", {}, lab_id="LAB-05",
                          evidence_slug="transition-resource",
                          document="family-link.lab-run-manifest/v2")
        run.event_sink({"kind": "stage", "stage": "preflight", "status": "failed"})
        result = run.finalize(workflow_status="FAIL", experiment_verdict="UNMEASURED",
                              caller_completed=False, cleanup_succeeded=True,
                              terminal_reason="preflight rejected")
        self.assertEqual(result["document"], "family-link.lab-run-manifest/v2")
        self.assertEqual(result["workflow_status"], "FAIL")
        self.assertEqual(result["experiment_verdict"], "UNMEASURED")
        self.assertIsNone(result["artifacts"]["metrics"])
        self.assertIsNone(result["artifacts"]["serial_log"])
        self.assertTrue(result["artifacts"]["events"].endswith("events.jsonl"))
        terminal = [json.loads(row) for row in run.events_path.read_text().splitlines()
                    if json.loads(row).get("kind") == "run_terminal"]
        self.assertEqual(len(terminal), 1)
        self.assertEqual(terminal[0]["workflow_status"], "FAIL")
        self.assertEqual(terminal[0]["experiment_verdict"], "UNMEASURED")

    def test_generic_schema_examples_are_valid_and_versioned(self):
        schema_dir = Path(__file__).resolve().parents[2] / "docs/labs/schemas"
        for filename, expected_document in (
            ("run-manifest.v2.md", "family-link.lab-run-manifest/v2"),
            ("metrics.v2.md", "family-link.lab-metrics/v2"),
        ):
            with self.subTest(filename=filename):
                source = (schema_dir / filename).read_text()
                example = source.split("```json\n", 1)[1].split("\n```", 1)[0]
                payload = json.loads(example)
                self.assertEqual(payload["document"], expected_document)
                self.assertIn("run_id", payload)
        manifest_source = (schema_dir / "run-manifest.v1.md").read_text()
        self.assertIn("family-link.lab-run-manifest/v1", manifest_source)
        self.assertIn("family-link.lab-metrics/v1", (schema_dir / "metrics.v1.md").read_text())


if __name__ == "__main__":
    unittest.main()
