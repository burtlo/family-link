#!/usr/bin/env python3
"""Guard same-turn touch handling and nonblocking LVGL event delivery."""

import re
import unittest
from pathlib import Path


PACKAGE = Path(__file__).resolve().parents[1]
SOURCE = (PACKAGE / "main/lab10_ui_lvgl.c").read_text(encoding="utf-8")
HEADER = (PACKAGE / "main/lab10_ui_lvgl.h").read_text(encoding="utf-8")
APP_MAIN = (PACKAGE / "main/app_main.c").read_text(encoding="utf-8")


def function_body(name: str, source: str = SOURCE) -> str:
    marker = f"{name}"
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unterminated function body: {name}")


class TouchPollHandoffTest(unittest.TestCase):
    def test_outcome_panel_stays_visible_until_reverse_exit_starts(self):
        present = function_body("void lab10_ui_view_present_state(")
        stopped = present.index("state == LAB10_SCREEN_STOPPED")
        show_panel = present.index(
            "lv_obj_set_hidden(view->outcome_panel, false)", stopped
        )
        self.assertLess(stopped, show_panel)

        exit_body = function_body("void lab10_ui_view_exit_reverse(")
        hide_panel = exit_body.index(
            "lv_obj_set_hidden(view->outcome_panel, true)"
        )
        reverse_state = exit_body.index("lab10_ui_exit_reverse(&view->model)")
        ring_animation = exit_body.index(
            "start_circle_animation(view, view->ring,"
        )
        fab_animation = exit_body.index(
            "start_circle_animation(view, view->bloom,"
        )
        self.assertLess(hide_panel, reverse_state)
        self.assertLess(hide_panel, ring_animation)
        self.assertLess(hide_panel, fab_animation)

    def test_touch_submit_is_followed_by_same_turn_view_poll(self):
        body = function_body("handle_touch_and_poll(")
        submit = body.index("lab10_ui_handle_touch(")
        poll = body.index("lab10_ui_view_poll(")
        self.assertLess(submit, poll)

    def test_canvas_rail_and_palette_callbacks_use_the_handoff(self):
        for callback in (
            "on_canvas_event(",
            "on_rail_event(",
            "on_palette_event(",
        ):
            with self.subTest(callback=callback):
                body = function_body(callback)
                self.assertIn("handle_touch_and_poll(", body)
                self.assertNotIn("lab10_ui_handle_touch(", body)

    def test_terminal_presentation_publishes_without_console_io(self):
        body = function_body("present_capture_outcome(")
        self.assertNotIn("printf(", body)
        self.assertNotIn("ESP_LOG", body)
        self.assertNotIn("LAB10_CAPTURE_TERMINAL", body)
        publish = body.index("publish_pending_capture_outcome(")
        reject = body.index("return;", publish)
        present = body.index("lab10_ui_present_outcome(")
        self.assertLess(reject, present)

    def test_pending_outcome_is_claimed_without_overwrite_and_consumed_once(self):
        publish = function_body("publish_pending_capture_outcome(")
        self.assertIn("atomic_compare_exchange_strong_explicit(", publish)
        self.assertIn(
            "int expected = LAB10_CAPTURE_OUTCOME_NONE;", publish
        )

        take = function_body("lab10_ui_view_take_pending_terminal_outcome(")
        self.assertIn("atomic_exchange_explicit(", take)
        self.assertIn("LAB10_CAPTURE_OUTCOME_NONE", take)
        self.assertIn("*outcome =", take)

        self.assertIn("lab10_ui_view_take_pending_terminal_outcome(", HEADER)

    def test_terminal_marker_is_emitted_only_by_non_lvgl_app_main_loop(self):
        self.assertNotIn("LAB10_CAPTURE_TERMINAL", SOURCE)
        body = function_body("app_main(", APP_MAIN)
        self.assertRegex(
            body,
            re.compile(
                r"if\s*\(\s*lab10_ui_view_take_pending_terminal_outcome\(&terminal_outcome\)"
                r"\s*\)\s*\{\s*printf\(\"LAB10_CAPTURE_TERMINAL "
                r"outcome=%s terminal_event_count=1",
                re.DOTALL,
            ),
        )
        self.assertIn("vTaskDelay(pdMS_TO_TICKS(20));", body)

    def test_lvgl_input_render_and_timer_paths_have_no_serial_logging(self):
        for name in (
            "handle_touch_and_poll(",
            "on_canvas_event(",
            "on_rail_event(",
            "on_palette_event(",
            "on_palette_changed(",
            "on_transition(",
            "on_touch_routed(",
            "on_render_publish_pixel(",
            "on_render_invalidation(",
            "lab10_ui_view_scripted_poll(",
            "present_capture_outcome(",
            "lab10_ui_view_poll(",
        ):
            with self.subTest(function=name):
                body = function_body(name)
                self.assertNotIn("printf(", body)
                self.assertNotIn("ESP_LOG", body)

        timer = function_body("ui_poll_timer(", APP_MAIN)
        self.assertNotIn("printf(", timer)
        self.assertNotIn("ESP_LOG", timer)
        self.assertNotIn("LAB10_SCRIPTED_DRAWING", SOURCE)

    def test_each_lifecycle_event_uses_one_non_overwriting_atomic_slot(self):
        scripted_start_publish = function_body(
            "publish_pending_scripted_start("
        )
        self.assertIn(
            "atomic_compare_exchange_strong_explicit(",
            scripted_start_publish,
        )
        self.assertIn("bool expected = false;", scripted_start_publish)
        scripted_start_take = function_body(
            "lab10_ui_view_take_pending_scripted_start("
        )
        self.assertIn("atomic_exchange_explicit(", scripted_start_take)
        self.assertIn("false", scripted_start_take)
        self.assertIn("s_scripted_start_delivery_required", scripted_start_take)

        completion_publish = function_body(
            "publish_pending_scripted_completion("
        )
        self.assertIn(
            "atomic_compare_exchange_strong_explicit(", completion_publish
        )
        self.assertIn("int expected = 0;", completion_publish)
        self.assertIn(
            "atomic_store_explicit(&s_pending_scripted_completion_state, 2",
            completion_publish,
        )
        completion_take = function_body(
            "lab10_ui_view_take_pending_scripted_completion("
        )
        self.assertIn("atomic_compare_exchange_strong_explicit(", completion_take)
        self.assertIn("int expected = 2;", completion_take)
        self.assertIn(
            "atomic_store_explicit(&s_pending_scripted_completion_state, 0",
            completion_take,
        )

        diagnostic_publish = function_body(
            "publish_pending_scripted_diagnostic("
        )
        self.assertIn("atomic_compare_exchange_strong_explicit(", diagnostic_publish)
        self.assertIn("int expected = 0;", diagnostic_publish)
        self.assertIn(
            "atomic_store_explicit(&s_pending_scripted_diagnostic_state, 2",
            diagnostic_publish,
        )
        diagnostic_take = function_body(
            "lab10_ui_view_take_pending_scripted_diagnostic("
        )
        self.assertIn("atomic_compare_exchange_strong_explicit(", diagnostic_take)
        self.assertIn("int expected = 2;", diagnostic_take)
        self.assertIn(
            "atomic_store_explicit(&s_pending_scripted_diagnostic_state, 0",
            diagnostic_take,
        )

        terminal_publish = function_body("publish_pending_capture_outcome(")
        self.assertIn(
            "atomic_compare_exchange_strong_explicit(", terminal_publish
        )
        self.assertIn("int expected = LAB10_CAPTURE_OUTCOME_NONE;", terminal_publish)
        retry = function_body("publish_pending_scripted_events(")
        self.assertIn("scripted_start_marker_pending", retry)
        self.assertIn("scripted_completion_marker_pending", retry)
        self.assertIn("publish_pending_scripted_start()", retry)
        self.assertIn("publish_pending_scripted_completion(", retry)
        self.assertIn("publish_view_pending_scripted_diagnostic(view)", retry)
        completion = function_body("lab10_ui_view_scripted_poll(")
        self.assertIn(
            "atomic_store_explicit(&s_scripted_start_delivery_required, true",
            completion,
        )
        terminal = function_body("present_capture_outcome(")
        self.assertIn(
            "atomic_load_explicit(&s_scripted_start_delivery_required",
            terminal,
        )
        self.assertIn("lab10_capture_get_stats(", completion)
        self.assertIn("stats.status == LAB10_CAPTURE_STATUS_TERMINAL", completion)
        self.assertIn(
            ".completion_scheduler_accepted =\n                result == LAB10_UI_SCRIPTED_POLL_COMPLETE",
            completion,
        )
        self.assertIn("stats.queue_overflow_count", completion)
        self.assertIn("stats.trace_loss_count", completion)
        self.assertNotIn("lab10_capture_get_stats(", APP_MAIN)

    def test_old_diagnostic_delivery_releases_latch_for_the_new_capture(self):
        self.assertIn(
            "lab10_ui_scripted_diagnostic_snapshot_t snapshot;", HEADER
        )
        self.assertIn(
            "lab10_ui_scripted_diagnostic_lifecycle_t scripted_diagnostic_lifecycle;",
            HEADER,
        )
        publish_view = function_body(
            "publish_view_pending_scripted_diagnostic("
        )
        mailbox_attempt = publish_view.index(
            "if (!publish_pending_scripted_diagnostic(&view->scripted_diagnostic))"
        )
        failed_delivery = publish_view.index(
            "lab10_ui_scripted_diagnostic_finish_delivery(", mailbox_attempt
        )
        self.assertIn(
            "&view->scripted_diagnostic.snapshot, false);",
            publish_view[failed_delivery:],
        )
        self.assertIn("return false;", publish_view[failed_delivery:])
        successful_delivery = publish_view.index(
            "&view->scripted_diagnostic.snapshot, true);"
        )
        self.assertIn(
            "view->scripted_diagnostic = (lab10_ui_scripted_diagnostic_t){0};",
            publish_view[successful_delivery:],
        )

        entry = function_body("entry_animation_done(")
        no_pending = entry.index(
            "if (!s_anim_view->scripted_diagnostic_lifecycle.pending)"
        )
        self.assertIn(
            "lab10_ui_scripted_diagnostic_allow_next_session(",
            entry[no_pending:],
        )
        poll = function_body("lab10_ui_view_scripted_poll(")
        old_snapshot_delivery = poll.index(
            "publish_view_pending_scripted_diagnostic(view)"
        )
        capture_diagnostic = poll.index(
            "if (stats.status == LAB10_CAPTURE_STATUS_TERMINAL &&\n"
            "        !view->scripted_diagnostic_lifecycle.reported)"
        )
        self.assertLess(old_snapshot_delivery, capture_diagnostic)
        remember_session = poll.index(
            ".capture_session_id = view->capture_session_id,",
            capture_diagnostic,
        )
        self.assertLess(capture_diagnostic, remember_session)
        begin = poll.index("lab10_ui_scripted_diagnostic_begin(", remember_session)
        self.assertLess(remember_session, begin)
        self.assertIn("view->scripted_diagnostic = diagnostic;", poll[begin:])

    def test_app_main_drains_scripted_markers_before_terminal_marker(self):
        body = function_body("app_main(", APP_MAIN)
        start = body.index("lab10_ui_view_take_pending_scripted_start(")
        completion = body.index(
            "lab10_ui_view_take_pending_scripted_completion("
        )
        diagnostic = body.index(
            "lab10_ui_view_take_pending_scripted_diagnostic("
        )
        terminal = body.index("lab10_ui_view_take_pending_terminal_outcome(")
        self.assertLess(start, completion)
        self.assertLess(completion, diagnostic)
        self.assertLess(diagnostic, terminal)
        self.assertIn("LAB10_SCRIPTED_DRAWING status=started", body)
        self.assertIn("LAB10_SCRIPTED_DRAWING status=complete", body)
        self.assertIn("LAB10_SCRIPTED_DIAGNOSTIC terminal=%s", body)
        for field in (
            "accepted_points=", "strokes=", "palette_changes=",
            "accepted_color_mask=", "queue_overflow_count=",
            "trace_loss_count=", "duration_cap_count=",
            "terminal_event_count=", "completion_scheduler_accepted=",
        ):
            self.assertIn(field, body)
        self.assertIn("LAB10_CAPTURE_TERMINAL outcome=%s terminal_event_count=1", body)


if __name__ == "__main__":
    unittest.main()
