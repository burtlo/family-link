"""Compile and exercise the actual LAB-02 C page parser when IDF is installed."""

import configparser
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "client/labs/lab-02-bounded-inbox"


class Lab02NativeModelTests(unittest.TestCase):
    def _compiler_inputs(self):
        cc = shutil.which("cc")
        config = configparser.ConfigParser()
        config.read(ROOT / "config/host.defaults.ini")
        idf = Path(os.environ.get("IDF_PATH", config.get("idf", "path"))).expanduser()
        cjson = idf / "components/json/cJSON"
        if not cc or not (cjson / "cJSON.c").exists():
            self.skipTest("host C compiler or ESP-IDF cJSON source unavailable")
        return cc, cjson

    def test_page_parse_and_replacement(self):
        cc, cjson = self._compiler_inputs()
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "lab02_page_test"
            subprocess.run(
                [cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(LAB / "components/lab02/include"), "-I", str(cjson),
                 str(LAB / "tests/test_page_merge_native.c"),
                 str(LAB / "components/lab02/inbox_model.c"), str(cjson / "cJSON.c"),
                 "-o", str(output)],
                check=True, capture_output=True, text=True,
            )
            subprocess.run([str(output)], check=True, capture_output=True, text=True)

    def test_session_result_ownership_and_snap_deferral(self):
        cc, cjson = self._compiler_inputs()
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "lab02_session_test"
            subprocess.run(
                [cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
                 "-pthread", "-I", str(LAB / "tests/host_stubs"),
                 "-I", str(LAB / "components/lab02/include"), "-I", str(cjson),
                 str(LAB / "tests/test_session_native.c"),
                 str(LAB / "tests/session_host_stubs.c"),
                 str(LAB / "components/lab02/lab02_session.c"),
                 str(LAB / "components/lab02/inbox_model.c"),
                 str(LAB / "components/lab02/inbox_page_metadata.c"),
                 str(cjson / "cJSON.c"), "-o", str(output)],
                check=True, capture_output=True, text=True,
            )
            subprocess.run([str(output)], check=True, capture_output=True, text=True,
                           timeout=5)
