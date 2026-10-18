import contextlib
import io
import json
import re
import subprocess
import sys
import tempfile
import types
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[4]
PACKAGE = ROOT / "client/labs/lab-10-record-canvas"
REPORT = ROOT / "docs/labs/evidence/lab-10/REPORT-LAB-10-L10-09-20261018.md"

sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(PACKAGE / "tools"))

from lab_adapter_lab10 import (  # noqa: E402
    MAX_SERIAL_BYTES,
    _capture_serial_bounded,
    _print_capture_active,
)
import parse_lab_trace as parser  # noqa: E402


def _run_native_qualification() -> str:
    """Compile and execute the qualification binary used by this package."""

    with tempfile.TemporaryDirectory() as td:
        binary = Path(td) / "lab10_l109_qualification"
        compile_command = [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-pthread",
            f"-I{PACKAGE / 'components/lab10_ui/include'}",
            f"-I{PACKAGE / 'components/lab10_capture/include'}",
            f"-I{PACKAGE / 'components/lab10_render/include'}",
            str(PACKAGE / "components/lab10_ui/lab10_ui.c"),
            str(PACKAGE / "components/lab10_capture/lab10_capture.c"),
            str(PACKAGE / "components/lab10_render/lab10_render.c"),
            str(PACKAGE / "tests/test_lab10_l109_qualification.c"),
            "-o",
            str(binary),
        ]
        subprocess.run(
            compile_command,
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        result = subprocess.run(
            [str(binary)],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        return result.stdout


def _markdown_sections(text: str) -> dict[str, str]:
    matches = list(re.finditer(r"^## ([^\n]+)\n", text, re.MULTILINE))
    sections: dict[str, str] = {}
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        sections[match.group(1)] = text[match.end():end].strip()
    return sections


class _InterruptingSerial:
    instance = None

    def __init__(self, *_args, **_kwargs):
        self.closed = False
        type(self).instance = self

    def reset_input_buffer(self):
        return None

    def read(self, _size):
        raise KeyboardInterrupt

    def close(self):
        self.closed = True


class Lab10L109QualificationBehaviorTests(unittest.TestCase):
    def test_physical_entry_closes_with_reverse_ring_transition(self):
        app_main = (PACKAGE / "main/app_main.c").read_text(encoding="utf-8")
        view = (PACKAGE / "main/lab10_ui_lvgl.c").read_text(encoding="utf-8")
        self.assertIn(
            "lab10_ui_view_enter_ring_wipe(view, LAB10_RING_ORIGIN_X,\n"
            "                                          LAB10_RING_ORIGIN_Y)",
            app_main,
        )
        self.assertIn("else if (lab10_ui_state(&view->model) == LAB10_SCREEN_RECORDING)", app_main)
        self.assertIn("lab10_ui_view_stop_capture(view);", app_main)
        self.assertIn("view->entered_with_ring_wipe = true;", view)
        self.assertIn("if (view->entered_with_ring_wipe)", view)
        self.assertIn("start_circle_animation(view, view->ring, transition.origin_x", view)
        self.assertIn("transition.radius_start,\n                               transition.radius_end, LAB10_RING_DURATION_MS", view)
        self.assertIn("lab10_ui_fab_exit_transition(view->fab_origin_x, view->fab_origin_y)", view)

    def test_native_qualification_executes_terminal_and_cleanup_boundaries(self):
        output = _run_native_qualification()
        lines = output.splitlines()
        self.assertEqual(lines[-1], "LAB10_L109_QUALIFICATION_PASS")
        self.assertEqual(
            lines.count(
                "L10-09 active_exit_cancel_queue_drained=1 stale_session_rejected=1"
            ),
            1,
        )
        self.assertEqual(
            lines.count("L10-09 capture_duration_cap_and_memory_drift_bytes=0"),
            1,
        )
        self.assertEqual(
            lines.count("L10-09 renderer_lock_failure_observed=1 partial_alloc_outstanding=0"),
            1,
        )
        cancel_lines = [
            line for line in lines if line.startswith("L10-09 capture_cancel_latency_us=")
        ]
        self.assertEqual(len(cancel_lines), 1)
        self.assertGreaterEqual(int(cancel_lines[0].split("=", 1)[1]), 0)

    def test_reverse_exit_cancels_active_capture_before_release_and_invalidation(self):
        view_source = (
            PACKAGE / "main/lab10_ui_lvgl.c"
        ).read_text(encoding="utf-8")
        exit_completion = view_source[
            view_source.index("static void exit_animation_done") : view_source.index(
                "static void start_circle_animation"
            )
        ]
        cancel_index = exit_completion.index("lab10_capture_cancel")
        reset_index = exit_completion.index("lab10_render_reset")
        release_index = exit_completion.index("lab10_capture_release_result")
        invalidate_index = exit_completion.index(
            "s_anim_view->capture_session_id = LAB10_CAPTURE_INVALID_SESSION_ID"
        )
        self.assertIn("LAB10_CAPTURE_STATUS_ACTIVE", exit_completion)
        self.assertLess(cancel_index, reset_index)
        self.assertLess(cancel_index, release_index)
        self.assertLess(cancel_index, invalidate_index)
        self.assertLess(reset_index, release_index)
        self.assertLess(release_index, invalidate_index)

    def test_parser_executes_real_marker_lifecycle_and_rejects_duplicate_ready(self):
        payload = (
            b"LAB10_RUN run_id=qualification-run mode=scripted "
            b"workload=scripted-human-like-multicolor-180s-v1 adapter_version=v1\n"
            b"LAB10_READY build=qualification-build "
            b"trace_schema=lab10-record-trace/v1 physical_ring_wipe=1\n"
            b"LAB10_SCENARIO id=scripted-human-like-multicolor-180s-v1 status=started\n"
            b"LAB10_SCRIPTED_DRAWING status=complete source=ui_task hardware_touch=0 terminal=duration_cap duration_cap_ms=180000 accepted_points=4091 strokes=18 palette_changes=17 accepted_color_mask=0x3e\n"
            b"LAB10_SCENARIO id=scripted-human-like-multicolor-180s-v1 status=passed\n"
        )
        result = parser.parse_bytes(
            payload,
            expected_run_id="qualification-run",
            expected_mode="scripted",
        )
        self.assertEqual(result["workflow_status"], "PASS")
        self.assertEqual(result["experiment_verdict"], "UNMEASURED")
        self.assertEqual(result["markers"]["ready"][0]["build"], "qualification-build")
        self.assertFalse(result["diagnostics"]["trace_present"])

        with self.assertRaises(parser.ParseError):
            parser.parse_bytes(
                payload
                + b"LAB10_READY build=qualification-build "
                b"trace_schema=lab10-record-trace/v1 physical_ring_wipe=1\n"
            )

    def test_adapter_executes_banner_and_interrupt_cleanup(self):
        target = SimpleNamespace(kit_id="box3-test")
        args = SimpleNamespace(backend="managed-http")
        with io.StringIO() as buffer, contextlib.redirect_stdout(buffer):
            _print_capture_active(target, args, "scripted", 19)
            banner = buffer.getvalue()
        self.assertIn("CAPTURE ACTIVE", banner)
        self.assertIn("mode=scripted; kit=box3-test; remaining=19s", banner)
        self.assertIn("18 paced strokes", banner)
        self.assertIn("real palette selections", banner)
        self.assertIn("variable 36–52 ms cadence", banner)
        self.assertIn("through the 180-second capture", banner)
        self.assertIn("no synthetic hardware-touch evidence", banner)
        self.assertNotIn("no palette changes", banner)

        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(Serial=_InterruptingSerial)
            with patch.dict(sys.modules, {"serial": fake_serial}), patch.dict(
                "os.environ", {"LAB10_WORKLOAD": "synthetic-sparse-multicolor-v1"}
            ):
                with self.assertRaises(KeyboardInterrupt):
                    _capture_serial_bounded(
                        fake_host,
                        SimpleNamespace(device="/dev/fake"),
                        "qualification-cancel",
                        1,
                        "scripted",
                    )
            serial_path = Path(td) / "logs/device/labs/lab-10/qualification-cancel/serial.txt"
            self.assertLessEqual(serial_path.stat().st_size, MAX_SERIAL_BYTES)
            self.assertIn(
                "LAB10_CANCEL requested=1 reason=keyboard_interrupt",
                serial_path.read_text(),
            )
            self.assertIsNotNone(_InterruptingSerial.instance)
            self.assertTrue(_InterruptingSerial.instance.closed)

    def test_report_sections_have_parseable_boundaries_and_evidence_classes(self):
        report_text = REPORT.read_text(encoding="utf-8")
        sections = _markdown_sections(report_text)
        expected = {
            "Implemented",
            "Host-verified",
            "Hardware-verified",
            "Unmeasured",
            "Blocked",
            "Failure-boundary matrix",
            "Unresolved decisions",
            "Next experiment",
            "Final classification",
        }
        self.assertEqual(set(sections), expected)
        for name in expected:
            self.assertTrue(sections[name].strip(), name)
        self.assertIn("20261018-scripted-run-ed8040af", sections["Hardware-verified"])
        self.assertIn("alignment=4", sections["Hardware-verified"])
        self.assertIn("palette_changes=3", sections["Hardware-verified"])
        self.assertIn("strokes=3", sections["Hardware-verified"])
        self.assertIn("status=passed", sections["Hardware-verified"])
        self.assertNotIn("No hardware run was performed", report_text)
        self.assertNotIn("absence of a registered", report_text)
        self.assertIn("capture_cancel_latency_us=0", report_text)
        self.assertIn("Experiment verdict: `UNMEASURED`", sections["Final classification"])
        self.assertIn("host workflow status: `PASS`", sections["Final classification"])
        self.assertIn("encoder failure", sections["Unmeasured"].lower())
        self.assertIn("worker failure", sections["Unmeasured"].lower())
        self.assertIn("kit", sections["Blocked"].lower())
        self.assertIn("OPEN", sections["Unresolved decisions"])

    def test_manifest_preserves_local_no_encoder_boundary(self):
        manifest = json.loads(
            (PACKAGE / "manifest.lab.json").read_text(encoding="utf-8")
        )
        self.assertEqual(manifest["server_path"], "none")
        self.assertIn("local capture only", manifest["protocol"])
        self.assertIn("send flow or upload", manifest["exclusions"])

    def test_boot_gate_is_structurally_after_all_required_startup_checks(self):
        app_main = (PACKAGE / "main/app_main.c").read_text(encoding="utf-8")
        view = (PACKAGE / "main/lab10_ui_lvgl.c").read_text(encoding="utf-8")
        self.assertIn("bool lab10_ui_view_init", view)
        self.assertIn("return view_init_failed(view, \"capture\")", view)
        self.assertIn("return view_init_failed(view, \"renderer\")", view)
        self.assertIn("return view_init_failed(view, \"canvas_buffer\")", view)
        self.assertIn("void lab10_ui_view_deinit", view)
        self.assertIn("LAB10_INIT_FAILED ui_view", app_main)
        self.assertIn("LAB10_INIT_FAILED ui_timer", app_main)
        self.assertIn("if (s_view.poll_timer == NULL)", app_main)
        view_failure_block = app_main[
            app_main.index("if (!lab10_ui_view_init") : app_main.index("s_view.poll_timer =")
        ]
        timer_failure_block = app_main[
            app_main.index("if (s_view.poll_timer == NULL)") : app_main.index("lab10_ui_view_enter_fab_bloom")
        ]
        self.assertNotIn("LAB10_READY", view_failure_block)
        self.assertNotIn("LAB10_READY", timer_failure_block)
        self.assertLess(
            app_main.index("LAB10_INIT_FAILED ui_view"),
            app_main.index("lab10_ui_view_enter_fab_bloom"),
        )
        self.assertLess(
            app_main.index("LAB10_INIT_FAILED ui_timer"),
            app_main.index("lab10_ui_view_enter_fab_bloom"),
        )
        self.assertLess(
            app_main.index("lab10_ui_view_enter_fab_bloom"),
            app_main.index("LAB10_READY"),
        )
        self.assertIn("atomic_store_explicit(&s_physical_button_down, true", app_main)
        self.assertIn("lab10_ui_view_poll(view);", app_main)


if __name__ == "__main__":
    unittest.main()
