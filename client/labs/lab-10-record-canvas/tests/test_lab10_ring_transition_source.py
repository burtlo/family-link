#!/usr/bin/env python3
"""Guard the physical ring's H19 animation contract in the LVGL bridge."""

import re
import unittest
from pathlib import Path


PACKAGE = Path(__file__).resolve().parents[1]
SOURCE = (PACKAGE / "main/lab10_ui_lvgl.c").read_text(encoding="utf-8")


def function_body(name: str) -> str:
    start = SOURCE.index(name)
    opening = SOURCE.index("{", start)
    depth = 0
    for index in range(opening, len(SOURCE)):
        if SOURCE[index] == "{":
            depth += 1
        elif SOURCE[index] == "}":
            depth -= 1
            if depth == 0:
                return SOURCE[opening + 1 : index]
    raise AssertionError(f"unterminated function body: {name}")


class RingTransitionSourceTest(unittest.TestCase):
    def test_ring_animation_uses_h19_custom_bezier_quintic_easing(self):
        animation = function_body("static void start_circle_animation(")
        self.assertRegex(
            animation,
            re.compile(
                r"if\s*\(use_ring_easing\)\s*\{\s*"
                r"lv_anim_set_path_cb\(&animation, lv_anim_path_custom_bezier3\);"
                r"\s*LV_ANIM_SET_EASE_IN_OUT_QUINT\(&animation\);\s*"
                r"\}\s*else\s*\{\s*"
                r"lv_anim_set_path_cb\(&animation, lv_anim_path_ease_in_out\);",
                re.DOTALL,
            ),
        )

    def test_both_ring_directions_use_saved_origin_duration_and_ring_easing(self):
        entry = function_body("void lab10_ui_view_enter_ring_wipe(")
        exit_body = function_body("void lab10_ui_view_exit_reverse(")

        self.assertIn("view->ring_origin_x = center_x;", entry)
        self.assertIn("view->ring_origin_y = center_y;", entry)
        self.assertIn("lab10_ui_ring_entry_transition(center_x, center_y)", entry)
        self.assertRegex(
            entry,
            re.compile(
                r"start_circle_animation\(view, view->ring,.*?"
                r"LAB10_RING_DURATION_MS,\s*true,\s*entry_animation_done\);",
                re.DOTALL,
            ),
        )

        self.assertIn("if (view->entered_with_ring_wipe)", exit_body)
        self.assertIn(
            "lab10_ui_ring_exit_transition(view->ring_origin_x,\n"
            "                                          view->ring_origin_y)",
            exit_body,
        )
        self.assertRegex(
            exit_body,
            re.compile(
                r"start_circle_animation\(view, view->ring,.*?"
                r"LAB10_RING_DURATION_MS,\s*true,\s*exit_animation_done\);",
                re.DOTALL,
            ),
        )

    def test_fab_bloom_keeps_existing_easing_and_timings(self):
        entry = function_body("void lab10_ui_view_enter_fab_bloom(")
        exit_body = function_body("void lab10_ui_view_exit_reverse(")
        exit_done = function_body("static void exit_animation_done(")

        self.assertRegex(
            entry,
            re.compile(
                r"start_circle_animation\(view, view->bloom,.*?"
                r"transition\.radius_end, 380,\s*false,\s*entry_animation_done\);",
                re.DOTALL,
            ),
        )
        self.assertRegex(
            exit_body,
            re.compile(
                r"start_circle_animation\(view, view->bloom,.*?"
                r"transition\.radius_end, 400,\s*false,\s*exit_animation_done\);",
                re.DOTALL,
            ),
        )
        self.assertIn("show_full_reveal(view);", entry)
        self.assertIn("if (s_anim_view->entered_with_ring_wipe)", exit_done)
        self.assertIn("hide_full_reveal(s_anim_view);", exit_done)
        self.assertIn("show_full_reveal(s_anim_view);", exit_done)

    def test_ring_reveal_host_wraps_content_and_tracks_each_radius(self):
        animation = function_body("static void anim_circle_size(")
        init = function_body("bool lab10_ui_view_init(")

        self.assertIn("if (object == view->ring)", animation)
        self.assertIn("update_ring_reveal(view, radius);", animation)
        self.assertIn("lab10_ui_ring_reveal_clip(", SOURCE)
        self.assertRegex(
            init,
            re.compile(
                r"view->reveal_host\s*=\s*lv_obj_create\(view->root\);.*?"
                r"lv_obj_set_size\(view->reveal_host, LAB10_LCD_WIDTH,"
                r"\s*LAB10_LCD_HEIGHT\);.*?"
                r"lv_obj_set_overflow_visible\(view->reveal_host, false\);.*?"
                r"view->canvas\s*=\s*lv_canvas_create\(view->reveal_host\);.*?"
                r"view->rail\s*=\s*lv_obj_create\(view->reveal_host\);",
                re.DOTALL,
            ),
        )

    def test_outcome_is_hidden_before_reverse_and_content_restored_at_idle(self):
        exit_body = function_body("void lab10_ui_view_exit_reverse(")
        entry_done = function_body("static void entry_animation_done(")
        done = function_body("static void exit_animation_done(")

        self.assertLess(
            exit_body.index("lv_obj_set_hidden(view->outcome_panel, true);"),
            exit_body.index("lab10_ui_exit_reverse(&view->model);"),
        )
        self.assertIn("show_full_reveal(s_anim_view);", entry_done)
        self.assertIn("hide_full_reveal(s_anim_view);", done)
        self.assertLess(
            done.index("hide_full_reveal(s_anim_view);"),
            done.index("lab10_ui_finish_exit(&s_anim_view->model);"),
        )


if __name__ == "__main__":
    unittest.main()
