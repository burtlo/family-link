#!/usr/bin/env python3
"""Execute the bounded scripted scheduler against native capture/render code."""

import json
import re
import subprocess
import tempfile
import unittest
from pathlib import Path


PACKAGE = Path(__file__).resolve().parents[1]
ROOT = PACKAGE.parents[2]


class ScriptedDeviceContractTests(unittest.TestCase):
    def test_timing_telemetry_is_reported_by_app_task_with_profile_identity(self):
        app_main = (PACKAGE / "main/app_main.c").read_text(encoding="utf-8")
        bridge = (PACKAGE / "main/lab10_ui_lvgl.c").read_text(encoding="utf-8")

        self.assertIn("cadence_profile=%s", app_main)
        self.assertIn("LAB10_UI_SCRIPTED_CADENCE_PROFILE", app_main)
        self.assertIn("cadence_min_ms=36 cadence_max_ms=52", app_main)
        for field in (
            "ui_poll_count",
            "max_ui_poll_gap_us",
            "scripted_event_count",
            "max_event_deadline_lateness_us",
            "late_event_count",
        ):
            self.assertIn(field, app_main)
        self.assertNotIn("printf(", bridge)

    def test_invalid_terminal_count_fails_closed_in_ui_presentation(self):
        bridge = (PACKAGE / "main/lab10_ui_lvgl.c").read_text(encoding="utf-8")
        start = bridge.index("static void present_capture_outcome(")
        end = bridge.index("void lab10_ui_view_stop_capture(", start)
        present = bridge[start:end]

        self.assertIn("stats.status != LAB10_CAPTURE_STATUS_TERMINAL", present)
        self.assertIn("const bool terminal_stats_valid = stats.terminal_event_count == 1u", present)
        self.assertIn(
            "? capture_terminal_kind(stats.terminal_outcome)\n                : LAB10_UI_TERMINAL_FAILED",
            present,
        )
        self.assertNotIn("stats.terminal_event_count != 1)", present)

    def test_native_scheduler_capture_and_continuous_render(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            binary = Path(temp_dir) / "lab10_scripted_native_test"
            command = [
                "cc",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                f"-I{PACKAGE / 'components/lab10_ui/include'}",
                f"-I{PACKAGE / 'components/lab10_capture/include'}",
                f"-I{PACKAGE / 'components/lab10_render/include'}",
                str(PACKAGE / "components/lab10_ui/lab10_ui.c"),
                str(PACKAGE / "components/lab10_capture/lab10_capture.c"),
                str(PACKAGE / "components/lab10_render/lab10_render.c"),
                str(PACKAGE / "tests/test_lab10_scripted.c"),
                "-o",
                str(binary),
            ]
            subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
            result = subprocess.run(
                [str(binary)], cwd=ROOT, check=True, capture_output=True, text=True
            )
            self.assertIn("LAB10_SCRIPTED_NATIVE_TEST_PASS", result.stdout)
            self.assertIn(
                "LAB10_SCRIPTED_RENDERER_FINAL canvas_final_color_mask=0x3e",
                result.stdout,
            )

    def test_device_defaults_select_connected_segments(self):
        kconfig = (PACKAGE / "main/Kconfig.projbuild").read_text(encoding="utf-8")
        defaults = (PACKAGE / "sdkconfig.defaults").read_text(encoding="utf-8")
        scripted_defaults = (PACKAGE / "sdkconfig.defaults.scripted").read_text(
            encoding="utf-8"
        )
        bridge = (PACKAGE / "main/lab10_ui_lvgl.c").read_text(encoding="utf-8")
        manifest = json.loads((PACKAGE / "manifest.lab.json").read_text())

        self.assertIn("default LAB10_RENDER_MODE_SEGMENT", kconfig)
        self.assertIn("CONFIG_LAB10_RENDER_MODE_SEGMENT=y", defaults)
        self.assertIn("CONFIG_LAB10_RENDER_MODE_SEGMENT=y", scripted_defaults)
        self.assertIn("return LAB10_RENDER_MODE_SEGMENT;", bridge)
        self.assertEqual(
            manifest["rendering_policy"]["hardware_selection"],
            "package-local LAB10_RENDER_MODE Kconfig choice; default segment candidate for pen-like same-stroke drawing; direct_point remains an explicitly labeled control",
        )

    def test_canvas_diagnostic_resets_per_session_and_is_bounded(self):
        bridge = (PACKAGE / "main/lab10_ui_lvgl.c").read_text(encoding="utf-8")
        header = (PACKAGE / "main/lab10_ui_lvgl.h").read_text(encoding="utf-8")
        ui_header = (PACKAGE / "components/lab10_ui/include/lab10_ui.h").read_text(
            encoding="utf-8"
        )
        app_main = (PACKAGE / "main/app_main.c").read_text(encoding="utf-8")

        reset_start = bridge.index("static void reset_canvas_color_diagnostics(")
        reset_end = bridge.index("static void on_render_publish_pixel(", reset_start)
        reset = bridge[reset_start:reset_end]
        for field in (
            "renderer_published_color_mask",
            "canvas_sampled_color_mask",
            "canvas_readback_color_mask",
            "canvas_color_match_mask",
        ):
            self.assertIn(f"view->{field} = 0u", reset)
            self.assertIn(field, header)
        self.assertIn("view->canvas_buffer != NULL", reset)
        self.assertIn("draw_buf != NULL && draw_buf->data != NULL", reset)

        format_start = bridge.index("static bool canvas_readback_format_supported(")
        format_end = bridge.index(
            "static bool publish_view_pending_scripted_diagnostic(", format_start
        )
        formats = bridge[format_start:format_end]
        for color_format in (
            "LV_COLOR_FORMAT_RGB565",
            "LV_COLOR_FORMAT_RGB888",
            "LV_COLOR_FORMAT_XRGB8888",
            "LV_COLOR_FORMAT_ARGB8888",
        ):
            self.assertIn(color_format, formats)
        self.assertIn("default:", formats)
        self.assertIn("return false", formats)

        match_start = bridge.index("static bool canvas_sample_matches_ink_color(")
        match_end = bridge.index("static uint8_t canvas_readback_color_bit(", match_start)
        color_match = bridge[match_start:match_end]
        self.assertIn("((red >> 3) * 2106u) >> 8", color_match)
        self.assertIn("((green >> 2) * 1037u) >> 8", color_match)
        self.assertIn("((blue >> 3) * 2106u) >> 8", color_match)

        scan_start = bridge.index("static uint8_t canvas_final_ink_color_mask(")
        scan_end = bridge.index("static void reset_canvas_color_diagnostics(", scan_start)
        final_scan = bridge[scan_start:scan_end]
        self.assertIn("logical_y < LAB10_LOGICAL_HEIGHT", final_scan)
        self.assertIn("logical_x < LAB10_LOGICAL_WIDTH", final_scan)
        self.assertRegex(ui_header, r"#define LAB10_LOGICAL_WIDTH 120\b")
        self.assertRegex(ui_header, r"#define LAB10_LOGICAL_HEIGHT 120\b")
        self.assertIn("logical_x * 2", final_scan)
        self.assertIn("logical_y * 2", final_scan)
        self.assertEqual(final_scan.count("lv_canvas_get_px("), 1)
        self.assertIn("canvas_readback_color_bit(sample, format)", final_scan)
        self.assertIn("LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK", final_scan)

        entry_start = bridge.index("static void entry_animation_done(")
        entry_end = bridge.index("static void exit_animation_done(", entry_start)
        entry = bridge[entry_start:entry_end]
        self.assertLess(
            entry.index("!lab10_render_begin("),
            entry.index("reset_canvas_color_diagnostics(s_anim_view)"),
        )
        self.assertEqual(entry.count("reset_canvas_color_diagnostics("), 1)

        publish_start = bridge.index("static void on_render_publish_pixel(")
        publish_end = bridge.index("static void on_render_invalidation(", publish_start)
        publish = re.sub(r"\s+", " ", bridge[publish_start:publish_end])
        self.assertEqual(publish.count("lv_canvas_get_px("), 1)
        self.assertIn("view->canvas_readback_supported", publish)
        self.assertIn("view->canvas_sampled_color_mask !=", publish)
        self.assertIn("(view->canvas_sampled_color_mask & color_bit) == 0u", publish)
        self.assertLess(
            publish.index("view->canvas_sampled_color_mask |= color_bit"),
            publish.index("lv_canvas_get_px("),
        )
        self.assertNotIn("ESP_LOG", publish)

        marker_start = app_main.index('"LAB10_SCRIPTED_DIAGNOSTIC terminal=%s')
        marker_end = app_main.index(
            "if (lab10_ui_view_take_pending_terminal_outcome", marker_start
        )
        marker = app_main[marker_start:marker_end]
        for field in (
            "renderer_published_color_mask",
            "canvas_sampled_color_mask",
            "canvas_readback_color_mask",
            "canvas_color_match_mask",
            "canvas_final_color_mask",
            "canvas_readback_supported",
            "canvas_color_format",
        ):
            self.assertIn(field, marker)
        self.assertIn("LAB10_SCRIPTED_DRAWING status=complete", app_main)

    def test_scripted_diagnostic_drain_is_session_scoped_and_retryable(self):
        bridge = (PACKAGE / "main/lab10_ui_lvgl.c").read_text(encoding="utf-8")
        header = (PACKAGE / "main/lab10_ui_lvgl.h").read_text(encoding="utf-8")
        app_main = (PACKAGE / "main/app_main.c").read_text(encoding="utf-8")

        self.assertIn(
            "lab10_ui_scripted_diagnostic_snapshot_t snapshot;", header
        )
        self.assertIn(
            "lab10_ui_scripted_diagnostic_lifecycle_t scripted_diagnostic_lifecycle;",
            header,
        )
        bridge_publish_start = bridge.index(
            "static bool publish_view_pending_scripted_diagnostic("
        )
        bridge_publish_end = bridge.index(
            "bool lab10_ui_view_take_pending_scripted_start(",
            bridge_publish_start,
        )
        publish = bridge[bridge_publish_start:bridge_publish_end]
        self.assertIn("lab10_render_get_metrics(", publish)
        self.assertIn("renderer_drained", publish)
        self.assertIn("!view->scripted_diagnostic_lifecycle.snapshot_frozen", publish)
        self.assertIn("canvas_final_ink_color_mask(view)", publish)
        self.assertLess(
            publish.index("renderer_metrics_available && renderer_drained"),
            publish.index("canvas_final_ink_color_mask(view)"),
        )
        self.assertEqual(publish.count("canvas_final_ink_color_mask(view)"), 1)
        self.assertIn("lab10_ui_scripted_diagnostic_prepare_delivery(", publish)
        self.assertIn("lab10_ui_scripted_diagnostic_finish_delivery(", publish)
        for field in (
            "renderer_published_color_mask",
            "canvas_sampled_color_mask",
            "canvas_readback_color_mask",
            "canvas_color_match_mask",
            "canvas_color_format",
            "canvas_readback_supported",
        ):
            self.assertIn(f"{field} = view->{field}", publish)
            self.assertIn(f"{field}", header)
        self.assertIn(".canvas_final_color_mask = canvas_final_color_mask", publish)
        self.assertIn(
            "publish_pending_scripted_diagnostic(&view->scripted_diagnostic)",
            publish,
        )
        self.assertLess(
            publish.index(
                "publish_pending_scripted_diagnostic(&view->scripted_diagnostic)"
            ),
            publish.index("lab10_ui_scripted_diagnostic_finish_delivery("),
        )
        self.assertIn("view->scripted_diagnostic_lifecycle.pending", publish)
        self.assertIn("false);", publish)
        self.assertIn("view->scripted_diagnostic = (lab10_ui_scripted_diagnostic_t){0};", publish)

        entry_start = bridge.index("static void entry_animation_done(")
        entry_end = bridge.index("static void exit_animation_done(", entry_start)
        entry = bridge[entry_start:entry_end]
        self.assertLess(
            entry.index("!lab10_render_begin("),
            entry.index(
                "if (!s_anim_view->scripted_diagnostic_lifecycle.pending)"
            ),
        )
        self.assertIn(
            "lab10_ui_scripted_diagnostic_allow_next_session(", entry
        )

        terminal_start = bridge.index(
            "lab10_ui_scripted_diagnostic_t diagnostic = {\n"
            "            .terminal_outcome ="
        )
        terminal_end = bridge.index("        };", terminal_start)
        terminal = bridge[terminal_start:terminal_end]
        for field in (
            "capture_session_id = view->capture_session_id",
            "accepted_points = stats.accepted_point_count",
            "renderer_published_color_mask =",
            "canvas_sampled_color_mask =",
            "canvas_readback_color_mask =",
            "canvas_color_match_mask =",
            "canvas_final_color_mask = 0u",
            "canvas_color_format =",
            "canvas_readback_supported =",
        ):
            self.assertIn(field, terminal)
        poll = bridge[bridge.index("void lab10_ui_view_scripted_poll("):]
        self.assertIn("!view->scripted_diagnostic_lifecycle.reported", poll)
        self.assertIn(
            "lab10_ui_scripted_diagnostic_begin(", poll
        )

        marker_start = app_main.index('"LAB10_SCRIPTED_DIAGNOSTIC terminal=%s')
        marker_end = app_main.index(
            "if (lab10_ui_view_take_pending_terminal_outcome", marker_start
        )
        marker = app_main[marker_start:marker_end]
        self.assertIn("capture_session_id=%\" PRIu64", marker)
        self.assertIn("renderer_drain_complete=%u", marker)
        self.assertIn("canvas_final_color_mask=0x%02x", marker)
        self.assertIn("scripted_diagnostic.snapshot.capture_session_id", marker)


if __name__ == "__main__":
    unittest.main()
