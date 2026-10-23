"""LAB-15 registry, parser, allocation, and adapter checks."""
from __future__ import annotations

import importlib.util
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "client/labs/lab-15-capture-memory-envelope"
FIXTURE = LAB / "tests/fixtures/scripted_catalog_serial.txt"
sys.path.insert(0, str(ROOT / "scripts"))

_PARSER_SPEC = importlib.util.spec_from_file_location(
    "parse_lab15_trace", LAB / "tools/parse_lab_trace.py"
)
parser = importlib.util.module_from_spec(_PARSER_SPEC)
sys.modules[_PARSER_SPEC.name] = parser
_PARSER_SPEC.loader.exec_module(parser)

from lab_adapter_lab15 import Adapter  # noqa: E402
from lab_adapter_registry import resolve_adapter  # noqa: E402
from lab_registry import load_registry  # noqa: E402
from types import SimpleNamespace  # noqa: E402


class Lab15HostTests(unittest.TestCase):
    def test_registry_lists_lab15(self) -> None:
        specs = load_registry()
        spec = specs["lab15"]
        self.assertEqual(spec.lab_id, "LAB-15")
        self.assertEqual(spec.adapter_key, "lab15")
        self.assertEqual(resolve_adapter(spec).__class__.__module__, "lab_adapter_lab15")

    def test_package_host_tests_pass(self) -> None:
        tests = [
            LAB / "tests/test_lab15_allocation_math.py",
            LAB / "tests/test_lab15_parser.py",
            LAB / "tests/test_lab15_terminals.py",
        ]
        for test_path in tests:
            completed = subprocess.run(
                [sys.executable, str(test_path)],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(
                completed.returncode,
                0,
                msg=f"{test_path.name} failed:\n{completed.stdout}\n{completed.stderr}",
            )

    def test_manifest_experiment_inputs(self) -> None:
        import json

        manifest = json.loads((LAB / "manifest.lab.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["experiment_inputs"], list(parser.EXPERIMENT_INPUTS))

    def test_s21_scripted_variant_and_local_backend(self) -> None:
        spec = SimpleNamespace(
            variants={"manual": object(), "scripted": object()},
            default_variant="manual",
        )
        adapter = Adapter()
        code = adapter.run(
            None,
            None,
            spec,
            SimpleNamespace(backend="local", variant=None, mode="scripted"),
            mode="scripted",
        )
        self.assertNotEqual(code, 0)
        self.assertNotEqual(adapter.last_workflow, "PASS")
        self.assertEqual(adapter.bound_ports, [])

    def test_fixture_serial_parses_pass(self) -> None:
        result = parser.parse(FIXTURE.read_text(encoding="utf-8"), mode="scripted")
        self.assertEqual(result["experiment_verdict"], "PASS")


if __name__ == "__main__":
    unittest.main()
