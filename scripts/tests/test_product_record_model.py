"""L1 tests for product record capture (P7)."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RECORD = ROOT / "client/shared/record_capture"
PROMOTED = RECORD / "promoted"
INCLUDE = [
    RECORD / "fl_product_record/include",
    PROMOTED / "lab10_ui/include",
    PROMOTED / "lab10_capture/include",
    PROMOTED / "lab10_render/include",
]
SOURCES = [
    RECORD / "fl_product_record/session.cpp",
    RECORD / "fl_product_record/storage.cpp",
    RECORD / "fl_product_record/media_file.cpp",
    RECORD / "fl_product_record/flsk2.cpp",
    PROMOTED / "lab10_ui/lab10_ui.c",
    PROMOTED / "lab10_capture/lab10_capture.c",
    PROMOTED / "lab10_render/lab10_render.c",
    RECORD / "tests/product_record_model_test.cpp",
]


def find_cxx() -> str:
    for name in ("c++", "g++", "clang++"):
        found = shutil.which(name)
        if found:
            return found
    raise RuntimeError("no c++, g++, or clang++ compiler on PATH")


class ProductRecordModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="product-record-model-")
        cls.binary = Path(cls.temp.name) / "product_record_model_test"
        cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
        if cc is None:
            raise RuntimeError("no C compiler on PATH")
        obj_dir = Path(cls.temp.name) / "objs"
        obj_dir.mkdir()
        includes = []
        for path in INCLUDE:
            includes.extend(["-I", str(path)])
        objects = []
        for src in SOURCES:
            obj = obj_dir / (src.stem + ".o")
            if src.suffix == ".c":
                subprocess.run(
                    [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", *includes, "-c", str(src), "-o", str(obj)],
                    check=True,
                    cwd=ROOT,
                )
            else:
                subprocess.run(
                    [
                        find_cxx(),
                        "-std=c++14",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-pedantic",
                        *includes,
                        "-c",
                        str(src),
                        "-o",
                        str(obj),
                    ],
                    check=True,
                    cwd=ROOT,
                )
            objects.append(str(obj))
        subprocess.run([find_cxx(), *objects, "-o", str(cls.binary)], check=True, cwd=ROOT)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def _run(self, name: str) -> None:
        subprocess.run([str(self.binary), name], check=True, cwd=ROOT)

    def test_p7_geometry(self):
        self._run("geometry")

    def test_p7_mapping(self):
        self._run("mapping")

    def test_p7_capture_vectors(self):
        self._run("capture")

    def test_p7_session_storage(self):
        self._run("session")

    def test_p7_media_stream(self):
        self._run("media")

    def test_p7_ink_overflow(self):
        self._run("overflow")

    def test_p5_wipe_and_capture(self):
        self._run("wipe")


if __name__ == "__main__":
    unittest.main()
