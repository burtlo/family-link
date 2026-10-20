"""Focused L1 tests that compile and execute the production LAB-06 C model."""
import json
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
LAB = ROOT / "client/labs/lab-06-stage-shell"


class Lab06ModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="lab06-model-")
        cls.binary = pathlib.Path(cls.temp.name) / "lab06_model_test"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I", str(LAB / "components/lab06_model/include"),
            "-I", str(LAB / "main"),
            str(LAB / "components/lab06_model/lab06_model.c"),
            str(LAB / "tests/model_test.c"), "-o", str(cls.binary),
        ], check=True, cwd=ROOT)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_model(self, scenario):
        subprocess.run([str(self.binary), scenario], check=True, cwd=ROOT)

    def test_lab06_single_writer(self):
        self.run_model("single_writer")

    def test_lab06_terminal_operation_id_reuse(self):
        self.run_model("terminal_id_reuse")

    def test_lab06_geometry(self):
        self.run_model("geometry")

    def test_lab06_idempotent_enter(self):
        self.run_model("idempotent_enter")

    def test_lab06_data_during_exit(self):
        self.run_model("data_during_exit")

    def test_lab06_stale_panel(self):
        self.run_model("stale_panel")

    def test_lab06_cancel(self):
        self.run_model("cancel")

    def test_lab06_cancel_schedule(self):
        self.run_model("cancel_schedule")

    def test_lab06_level2_control(self):
        self.run_model("level2_control")

    def test_lab06_pending_capacity(self):
        self.run_model("pending_capacity")
        readme = (LAB / "README.md").read_text()
        match = re.search(r"\| `max_concurrent_operations` \| (\d+) \|", readme)
        self.assertIsNotNone(match)
        self.assertEqual(int(match.group(1)), 9)

    def test_lab06_data_levels(self):
        self.run_model("data_levels")

    def test_lab06_cycle(self):
        self.run_model("cycle")

    def test_lab06_event_overflow(self):
        self.run_model("overflow")

    def test_lab06_event_queue_terminals(self):
        self.run_model("queue_terminals")

    def test_lab06_manifest_and_scenarios(self):
        manifest = json.loads((LAB / "manifest.lab.json").read_text())
        self.assertEqual(set(manifest), {
            "document", "lab_id", "lab_revision", "firmware_path", "server_path",
            "fixture_schema", "fixture_revision", "protocol", "trace_schema",
        })
        self.assertEqual(manifest["document"], "family-link.lab-manifest/v1")
        self.assertEqual(manifest["lab_id"], "LAB-06")
        self.assertEqual(manifest["server_path"], "none")
        self.assertEqual(manifest["fixture_revision"], "lab06-stage-shell/v1")
        self.assertEqual(manifest["trace_schema"], "lab06-trace/v1")
        fixture = json.loads((LAB / "fixtures/scenarios.json").read_text())
        self.assertEqual(fixture["experiment_inputs"], [
            "H-cycle", "H-data", "H-data-during-exit", "H-level2-control",
            "H-destroyed", "H-cancel",
        ])
        decided = [row["id"] for row in fixture["scenarios"] if row["class"] == "decided"]
        self.assertEqual(decided, fixture["experiment_inputs"])
        placeholder = next(row for row in fixture["scenarios"] if row["id"] == "H-placeholder")
        self.assertEqual(placeholder["class"], "exploratory")
        self.assertEqual(placeholder["observation"]["placeholder"], 1)
        self.assertNotIn("H-placeholder", fixture["experiment_inputs"])
        trace = json.loads((LAB / "trace-schema.json").read_text())
        self.assertEqual(trace["$id"], manifest["trace_schema"])
        self.assertEqual(trace["properties"]["schema"]["const"], manifest["trace_schema"])
        self.assertIn("transition_terminal", trace["properties"]["event"]["enum"])
