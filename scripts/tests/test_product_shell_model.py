"""L1 tests for the promoted product stage shell model in client/shared."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SHARED = ROOT / "client/shared/stage_shell"


class ProductShellModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="product-shell-model-")
        cls.binary = pathlib.Path(cls.temp.name) / "fl_shell_model_test"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I", str(SHARED / "fl_shell_model/include"),
            "-I", str(SHARED / "tests"),
            str(SHARED / "fl_shell_model/fl_shell_model.c"),
            str(SHARED / "tests/shell_model_test.c"), "-o", str(cls.binary),
        ], check=True, cwd=ROOT)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_model(self, scenario):
        subprocess.run([str(self.binary), scenario], check=True, cwd=ROOT)

    def test_single_writer(self):
        self.run_model("single_writer")

    def test_stale_panel(self):
        self.run_model("stale_panel")

    def test_data_during_exit(self):
        self.run_model("data_during_exit")

    def test_cycle_timer_stable(self):
        self.run_model("cycle")

    def test_data_levels_do_not_rebuild_shell(self):
        self.run_model("data_levels")

    def test_level2_rejected_on_data_path(self):
        self.run_model("level2_control")


if __name__ == "__main__":
    unittest.main()
