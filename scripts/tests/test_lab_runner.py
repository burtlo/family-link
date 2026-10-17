import json
import subprocess
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from lab_adapter_registry import resolve_adapter
from lab_registry import get_lab, LabRegistryError
from lab_runner import RunHooks, run_with_adapter
from lab_run_evidence import EvidenceRun
import lab_host


class SharedLabRunnerTests(unittest.TestCase):
    def test_generic_v2_manifest_always_has_firmware_build_fingerprint(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for run_id, firmware, expected in (
                ("fingerprint-unknown", {}, None),
                ("fingerprint-known", {"build_fingerprint": "sha256:compiled-inputs"},
                 "sha256:compiled-inputs"),
            ):
                evidence = EvidenceRun(root, run_id, {"firmware": firmware},
                                       lab_id="LAB-99", evidence_slug="lab-test")
                manifest = evidence.finalize(
                    workflow_status="FAIL", experiment_verdict="UNMEASURED",
                    caller_completed=False, cleanup_succeeded=True,
                    terminal_reason="preflight rejected",
                )
                self.assertIn("build_fingerprint", manifest["firmware"])
                self.assertEqual(manifest["firmware"]["build_fingerprint"], expected)

    def run_fake(self, root: Path, mode: str, *, server=None):
        def capture(_target, run_id):
            path = root / "logs/device/labs/lab-test" / run_id / "serial.txt"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("ready\n", encoding="utf-8")
            return path

        return run_with_adapter(
            repo_root=root, lab_id="LAB-99", evidence_slug="lab-test",
            variant="default", mode=mode, requested_run_id=f"runner-{mode}",
            hooks=RunHooks(lambda: {"kit_id": "kit-a"}, lambda _target: "built",
                           capture, server=server),
        )

    def test_manual_and_scripted_modes_share_manifest_and_event_shape(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for mode in ("manual", "scripted"):
                self.assertEqual(self.run_fake(root, mode), 0)
                run_dir = root / "docs/labs/evidence/lab-test/runs" / f"runner-{mode}"
                manifest = json.loads((run_dir / "run-manifest.json").read_text())
                events = [json.loads(line) for line in (run_dir / "events.jsonl").read_text().splitlines()]
                self.assertTrue(all(row.get("source") in {"host", "server", "device", "unknown"}
                                    for row in events))
                self.assertTrue(all(row.get("source") == "host" for row in events
                                    if row.get("kind") in {"stage", "run_terminal"}))
                self.assertEqual(manifest["document"], "family-link.lab-run-manifest/v2")
                self.assertEqual(manifest["capture"]["mode"], mode)
                self.assertEqual(manifest["workflow_status"], "PASS")
                self.assertEqual(manifest["event_loss"]["host_journal"],
                                 {"status": "NONE", "dropped_events": 0})
                self.assertEqual(manifest["event_loss"]["firmware_trace"],
                                 {"status": "UNKNOWN", "dropped_events": None})
                expected_serial_log = f"logs/device/labs/lab-test/runner-{mode}/serial.txt"
                self.assertEqual(manifest["artifacts"]["serial_log"], expected_serial_log)
                self.assertTrue((root / expected_serial_log).is_file())
                self.assertIn("source", events[0])
                self.assertIn("monotonic_ns", events[0])
                self.assertIn("outcome", events[0])
                self.assertTrue(any(row.get("kind") == "stage" and row.get("outcome") == "not_configured"
                                    and row.get("stage") == "service" for row in events))

    def test_service_hook_is_dispatched_and_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            called = []
            self.assertEqual(self.run_fake(root, "manual", server=lambda context: called.append(context.run_id)), 0)
            self.assertEqual(called, ["runner-manual"])
            events_path = root / "docs/labs/evidence/lab-test/runs/runner-manual/events.jsonl"
            events = [json.loads(line) for line in events_path.read_text().splitlines()]
            self.assertTrue(any(row.get("stage") == "service" and row.get("outcome") == "passed"
                                for row in events))

    def test_event_source_defaults_unknown_and_manifest_preserves_observed_firmware_loss(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            evidence = EvidenceRun(root, "source-loss", {}, lab_id="LAB-99", evidence_slug="lab-test")
            evidence.event_sink({"kind": "legacy"})
            evidence.event_sink({"source": "device", "kind": "device_trace"})
            evidence.update_context({"event_loss": {"firmware_trace": {
                "status": "OBSERVED", "dropped_events": 2}}})
            manifest = evidence.finalize(workflow_status="FAIL", experiment_verdict="UNMEASURED",
                                         caller_completed=False, cleanup_succeeded=True)
            events = [json.loads(row) for row in evidence.events_path.read_text().splitlines()]
            self.assertEqual([row["source"] for row in events], ["unknown", "device", "host"])
            self.assertEqual(manifest["event_loss"]["host_journal"]["dropped_events"], 0)
            self.assertEqual(manifest["event_loss"]["firmware_trace"]["status"], "OBSERVED")
            self.assertEqual(manifest["event_loss"]["firmware_trace"]["dropped_events"], 2)

    def test_lab05_registry_runs_both_modes_without_lab02_workflow_access(self):
        import yaml

        spec = get_lab("lab05")
        registry_doc = yaml.safe_load(
            (Path(__file__).resolve().parents[2] / "config/labs.registry.yaml").read_text()
        )
        self.assertEqual(spec.adapter_key, "generic")
        self.assertEqual(spec.lab_id, "LAB-05")
        self.assertNotIn("workflow", registry_doc["labs"]["lab05"])

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            serial_path = root / "logs/device/labs/lab-05/serial.txt"
            serial_path.parent.mkdir(parents=True)
            serial_path.write_text("LAB05_READY server=none\n", encoding="utf-8")
            target = type("Target", (), {"kit_id": "box3", "device": "/dev/fake"})()

            for mode in ("manual", "scripted"):
                run_id = f"lab05-{mode}"
                for name in list(sys.modules):
                    if name == "lab02_evidence" or name.startswith("lab02_"):
                        del sys.modules[name]
                with patch.object(lab_host, "ROOT", root), \
                     patch.object(lab_host.ProjectConfig, "validate"), \
                     patch.object(lab_host, "get_lab", return_value=spec), \
                     patch.object(lab_host, "resolve_run_target", return_value=target), \
                     patch.object(lab_host, "capture_lab", return_value=serial_path), \
                     patch.object(lab_host, "load_lab02_workflow_config",
                                  side_effect=AssertionError("LAB-02 workflow settings accessed")), \
                     patch.object(sys, "argv", ["lab_host.py", "run", "lab05", "--no-build",
                                                 "--mode", mode, "--run-id", run_id]):
                    self.assertEqual(lab_host.main(), 0)

                self.assertFalse(
                    any(name == "lab02_evidence" or name.startswith("lab02_") for name in sys.modules),
                    "LAB-05 generic dispatch imported a LAB-02 module",
                )
                run_dir = root / "docs/labs/evidence/lab-05/runs" / run_id
                manifest = json.loads((run_dir / "run-manifest.json").read_text())
                events = [json.loads(line) for line in (run_dir / "events.jsonl").read_text().splitlines()]
                self.assertEqual(manifest["document"], "family-link.lab-run-manifest/v2")
                self.assertEqual(manifest["lab_id"], "LAB-05")
                self.assertEqual(manifest["capture"]["mode"], mode)
                self.assertEqual(manifest["workflow_status"], "PASS")
                self.assertTrue(any(row.get("stage") == "service"
                                    and row.get("outcome") == "not_configured" for row in events))

    def test_cleanup_failure_fails_workflow_and_keeps_terminal_artifact(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            def fail_cleanup(_context):
                raise RuntimeError("cleanup")
            def capture(_target, run_id):
                path = root / "logs/device/labs/lab-test" / run_id / "serial.txt"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("partial\n", encoding="utf-8")
                return path
            result = run_with_adapter(
                repo_root=root, lab_id="LAB-99", evidence_slug="lab-test",
                variant="default", mode="manual", requested_run_id="cleanup-failure",
                hooks=RunHooks(lambda: object(), lambda _target: "built", capture,
                               cleanup=fail_cleanup),
            )
            self.assertEqual(result, 1)
            manifest = json.loads((root / "docs/labs/evidence/lab-test/runs/cleanup-failure/run-manifest.json").read_text())
            self.assertEqual(manifest["workflow_status"], "FAIL")
            self.assertEqual(manifest["capture"]["stages"]["cleanup"], "failed")

    def test_allowlist_rejects_unregistered_adapter_without_importing_it(self):
        spec = replace(get_lab("lab02"), adapter_key="arbitrary.module")
        with self.assertRaisesRegex(LabRegistryError, "Unsupported adapter key"):
            resolve_adapter(spec)

    def test_importing_generic_cli_does_not_import_lab02_modules(self):
        code = (
            "import sys; sys.path.insert(0, 'scripts'); import lab_host, lab_runner, lab_runner_adapter; "
            "assert not any(name.startswith('lab02_') for name in sys.modules)"
        )
        result = subprocess.run(
            [sys.executable, "-c", code], cwd=Path(__file__).resolve().parents[2],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_registry_cli_dispatches_generic_no_server_lab(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            spec = replace(get_lab("lab02"), adapter_key="generic")
            serial_path = root / "logs/device/labs/lab-test/registry-no-server/serial.txt"
            serial_path.parent.mkdir(parents=True)
            serial_path.write_text("ready\n", encoding="utf-8")
            target = type("Target", (), {"kit_id": "kit-a", "device": "/dev/fake"})()
            for name in list(sys.modules):
                if name == "lab02_evidence" or name.startswith("lab02_"):
                    del sys.modules[name]
            with patch.object(lab_host, "ROOT", root), \
                 patch.object(lab_host.ProjectConfig, "validate"), \
                 patch.object(lab_host, "get_lab", return_value=spec), \
                 patch.object(lab_host, "resolve_run_target", return_value=target), \
                 patch.object(lab_host, "capture_lab", return_value=serial_path), \
                 patch.object(sys, "argv", ["lab_host.py", "run", "lab02", "--no-build",
                                             "--run-id", "registry-no-server"]):
                self.assertEqual(lab_host.main(), 0)

            self.assertFalse(
                any(name == "lab02_evidence" or name.startswith("lab02_") for name in sys.modules),
                "generic registry dispatch imported a LAB-02 module during no-server execution",
            )

            run_dir = root / "docs/labs/evidence/lab-02/runs/registry-no-server"
            manifest = json.loads((run_dir / "run-manifest.json").read_text())
            events = [json.loads(line) for line in (run_dir / "events.jsonl").read_text().splitlines()]
            self.assertEqual(manifest["capture"]["mode"], "manual")
            self.assertEqual(manifest["workflow_status"], "PASS")
            self.assertTrue(any(row.get("stage") == "service" and row.get("outcome") == "not_configured"
                                for row in events))

    def test_registry_cli_dispatches_service_adapter_hook(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            spec = get_lab("lab02")
            target = type("Target", (), {"kit_id": "kit-a", "device": "/dev/fake"})()
            serial_path = root / "logs/device/labs/lab-test/registry-service/serial.txt"
            serial_path.parent.mkdir(parents=True)
            serial_path.write_text("ready\n", encoding="utf-8")
            service_calls = []

            class MockServiceAdapter:
                def run(self, host, config, spec, args, *, mode):
                    return run_with_adapter(
                        repo_root=host.ROOT, lab_id="LAB-99", evidence_slug="lab-test",
                        variant="default", mode=mode, requested_run_id=args.run_id,
                        hooks=RunHooks(lambda: target, lambda _target: "built",
                                       lambda _target, _rid: serial_path,
                                       server=lambda context: service_calls.append(context.run_id)),
                    )

            with patch.object(lab_host, "ROOT", root), \
                 patch.object(lab_host.ProjectConfig, "validate"), \
                 patch.object(lab_host, "get_lab", return_value=spec), \
                 patch("lab_adapter_registry.resolve_adapter", return_value=MockServiceAdapter()), \
                 patch.object(sys, "argv", ["lab_host.py", "run", "lab02", "--run-id",
                                             "registry-service"]):
                self.assertEqual(lab_host.main(), 0)

            self.assertEqual(service_calls, ["registry-service"])
            run_dir = root / "docs/labs/evidence/lab-test/runs/registry-service"
            manifest = json.loads((run_dir / "run-manifest.json").read_text())
            events = [json.loads(line) for line in (run_dir / "events.jsonl").read_text().splitlines()]
            self.assertEqual(manifest["workflow_status"], "PASS")
            self.assertTrue(any(row.get("stage") == "service" and row.get("outcome") == "passed"
                                for row in events))


if __name__ == "__main__":
    unittest.main()
