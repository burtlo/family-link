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
    _SESSION_SOURCES = (
        "tests/test_session_native.c",
        "tests/session_host_stubs.c",
        "components/lab02/lab02_session.c",
        "components/lab02/inbox_model.c",
        "components/lab02/inbox_page_metadata.c",
    )

    def _compiler_inputs(self):
        cc = shutil.which("cc")
        config = configparser.ConfigParser()
        config.read(ROOT / "config/host.defaults.ini")
        idf = Path(os.environ.get("IDF_PATH", config.get("idf", "path"))).expanduser()
        cjson = idf / "components/json/cJSON"
        if not cc or not (cjson / "cJSON.c").exists():
            self.skipTest("host C compiler or ESP-IDF cJSON source unavailable")
        return cc, cjson

    def _compile_and_run(self, cc, cjson, sources, output_name, *, extra_cflags=(), timeout=30,
                         link_cjson=True):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / output_name
            cmd = [
                cc, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
                "-pthread", "-I", str(LAB / "tests/host_stubs"),
                "-I", str(LAB / "components/lab02/include"), "-I", str(cjson),
                *extra_cflags,
                *(str(LAB / rel) for rel in sources),
            ]
            if link_cjson:
                cmd.append(str(cjson / "cJSON.c"))
            cmd.extend(["-o", str(output)])
            subprocess.run(cmd, check=True, capture_output=True, text=True)
            subprocess.run([str(output)], check=True, capture_output=True, text=True, timeout=timeout)

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
        self._compile_and_run(cc, cjson, self._SESSION_SOURCES, "lab02_session_test")

    def test_session_transport_updates_while_scroll_deferred(self):
        """LAB02-F002: transport events must not wait behind deferred page apply."""
        cc, cjson = self._compiler_inputs()
        sources = (
            "tests/test_session_transport_defer_native.c",
            *self._SESSION_SOURCES[1:],
        )
        self._compile_and_run(cc, cjson, sources, "lab02_transport_defer_test")

    def test_snap_overlap_releases_scroll_lock(self):
        """LAB02-F003: cancelling an in-flight snap must run on_done / clear scroll_lock."""
        cc, cjson = self._compiler_inputs()
        sources = (
            "tests/test_snap_overlap_native.c",
            "tests/host_stubs/lvgl/lvgl_host_stub.c",
            "components/lab02/snap.c",
            "components/lab02/instrumentation.c",
            "components/lab02/trace_emit.c",
        )
        self._compile_and_run(
            cc,
            cjson,
            sources,
            "lab02_snap_overlap_test",
            extra_cflags=("-I", str(LAB / "tests/host_stubs/lvgl")),
            timeout=10,
            link_cjson=False,
        )
