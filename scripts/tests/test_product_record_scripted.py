"""L2 scripted capture harness for product record (P7)."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RECORD = ROOT / "client/shared/record_capture"
PROMOTED = RECORD / "promoted"


class ProductRecordScriptedTests(unittest.TestCase):
    def test_native_scripted_capture_matches_lab10_contract(self):
        cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
        if cc is None:
            self.skipTest("no C compiler")
        with tempfile.TemporaryDirectory(prefix="product-record-scripted-") as temp_dir:
            binary = Path(temp_dir) / "product_record_scripted_test"
            command = [
                cc,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                f"-I{PROMOTED / 'lab10_ui/include'}",
                f"-I{PROMOTED / 'lab10_capture/include'}",
                f"-I{PROMOTED / 'lab10_render/include'}",
                str(PROMOTED / "lab10_ui/lab10_ui.c"),
                str(PROMOTED / "lab10_capture/lab10_capture.c"),
                str(PROMOTED / "lab10_render/lab10_render.c"),
                str(RECORD / "tests/product_record_scripted_test.c"),
                "-o",
                str(binary),
            ]
            subprocess.run(command, cwd=ROOT, check=True)
            result = subprocess.run([str(binary)], cwd=ROOT, check=True, capture_output=True, text=True)
            self.assertIn("LAB10_SCRIPTED_NATIVE_TEST_PASS", result.stdout)
            self.assertIn("LAB10_SCRIPTED_RENDERER_FINAL canvas_final_color_mask=0x3e", result.stdout)


if __name__ == "__main__":
    unittest.main()
