import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from lab_registry import (
    SNAPSHOT_DOCUMENT,
    build_catalog,
    get_lab,
    load_registry,
    operator_help_lines,
    resolve_variant,
)
from project_config import ROOT

SCRIPTS = Path(__file__).resolve().parents[1]


class LabRegistryTests(unittest.TestCase):
    def test_loads_lab02(self):
        registry = load_registry()
        self.assertIn("lab02", registry)
        spec = registry["lab02"]
        self.assertEqual(spec.lab_id, "LAB-02")
        self.assertEqual(spec.evidence_slug, "lab-02")
        self.assertTrue(spec.run_instructions)
        self.assertIn("lab-02-implementation-outline.md", spec.run_instructions_doc or "")
        self.assertTrue((ROOT / spec.project_dir / "CMakeLists.txt").is_file())

    def test_variants(self):
        spec = get_lab("lab02")
        recycle = resolve_variant(spec, "recycle")
        self.assertEqual(recycle.build_dir, Path("build/labs/lab-02-bounded-inbox"))
        default = resolve_variant(spec, None)
        self.assertEqual(default.name, "phaseb")

    def test_operator_help_hides_internal_variant_names(self):
        text = "\n".join(operator_help_lines(get_lab("lab02")))
        self.assertNotIn("phaseb", text.lower())
        self.assertNotIn("eager", text.lower())

    def test_lab02_capture_help_describes_generic_serial_capture(self):
        text = "\n".join(operator_help_lines(get_lab("lab02"), action="capture"))
        self.assertIn("Generic host serial capture", text)
        self.assertIn("No build, managed server, reset", text)
        self.assertNotIn("Managed capture", text)

    def test_cli_validate_and_dump(self):
        validate = subprocess.run(
            [sys.executable, str(SCRIPTS / "lab_registry.py"), "validate"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(validate.returncode, 0, validate.stderr)
        self.assertIn("labs.registry.yaml", validate.stdout)

        dump = subprocess.run(
            [sys.executable, str(SCRIPTS / "lab_registry.py"), "dump"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(dump.returncode, 0, dump.stderr)
        self.assertIn(SNAPSHOT_DOCUMENT, dump.stdout)

    def test_catalog_documents_schema(self):
        catalog = build_catalog()
        self.assertIn("schema_file", catalog)
        self.assertTrue(Path(str(catalog["schema_file"])).is_file())


if __name__ == "__main__":
    unittest.main()
