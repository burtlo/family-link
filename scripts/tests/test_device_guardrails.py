"""Device guardrail program (phases 0–3) — host tests only."""
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from device_guardrails.desk_record_send_check import verify_record_send_desk
from device_guardrails.lab16_serial import verify_lab16_capture
from device_guardrails.presentation_path import (
    scan_snippet,
    scan_text_for_timer_violation,
    scan_timer_path,
)
from device_guardrails.product_boot_serial import verify_incomplete_ui_boot_should_fail, verify_product_boot
from device_guardrails.quiescence_fault import (
    inject_lab16_fault,
    inject_lab16_quiesced,
    inject_product_fault,
    inject_product_quiesced,
    lab16_live_stack_job_errors,
    lab16_stack_job_errors,
    product_stop_without_ack,
)
from device_guardrails import presentation_invariants as pres
from device_guardrails.sdkconfig_baseline import check_draw_lines, check_sdkconfig_defaults
from firmware import device_build_report, product_flash_log_ok

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / "fixtures" / "device-serial"
VERIFY_LAB16 = ROOT / "client/labs/lab-16-attached-storage/tools/verify_capture.py"
DEFAULTS = ROOT / "client/device/sdkconfig.defaults"


class DeviceGuardrailsPhase0Tests(unittest.TestCase):
    def test_lab16_guru_fixture_rejected(self):
        text = (FIXTURES / "guru-sample.txt").read_text(encoding="utf-8")
        errors = verify_lab16_capture(text)
        self.assertTrue(any("Guru" in item or "forbidden" in item for item in errors))

    def test_lab16_verify_script_rejects_guru(self):
        path = FIXTURES / "guru-sample.txt"
        proc = subprocess.run(
            [sys.executable, str(VERIFY_LAB16), str(path)],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(proc.returncode, 0)

    def test_timer_path_fixture_detects_sdcard_mount(self):
        sample = """
        extern "C" void product_record_service(int64_t now_ms, int active) {
            bsp_sdcard_mount();
        }
        """
        errors = scan_text_for_timer_violation(sample)
        self.assertGreater(len(errors), 0)

    def test_timer_callee_mount_is_detected(self):
        sample = """
        void ensure_media(void) { bsp_sdcard_mount(); }
        void product_record_begin_wipe(void) { ensure_media(); }
        void on_login_timer(void) { product_record_begin_wipe(); }
        """
        errors = scan_snippet(sample)
        self.assertTrue(any("bsp_sdcard_mount" in item for item in errors))

    def test_live_timer_path_has_no_forbidden_calls(self):
        """PRES-PATH: the card is mounted at boot, never from on_login_timer."""
        self.assertEqual(scan_timer_path(), [])

    def test_http_result_local_fixture_fails(self):
        sample = """
        extern "C" void product_login_http_service(void *session, int64_t now_ms) {
            HttpResult result;
            (void)session;
            (void)now_ms;
        }
        """
        errors = scan_text_for_timer_violation(sample, function_name="product_login_http_service")
        self.assertGreater(len(errors), 0)

    def test_sdkconfig_baseline_matches_product_defaults(self):
        self.assertEqual(check_sdkconfig_defaults(DEFAULTS), [])

    def test_draw_lines_locked_at_16(self):
        self.assertEqual(check_draw_lines(), [])

    def test_sdkconfig_drift_detected(self):
        with tempfile.NamedTemporaryFile("w", suffix=".defaults", delete=False, encoding="utf-8") as tmp:
            text = DEFAULTS.read_text(encoding="utf-8").replace(
                "CONFIG_LV_MEM_SIZE=32768", "CONFIG_LV_MEM_SIZE=65536"
            )
            tmp.write(text)
            path = Path(tmp.name)
        errors = check_sdkconfig_defaults(path)
        self.assertTrue(any("LV_MEM_SIZE" in item for item in errors))


class DeviceGuardrailsPhase1Tests(unittest.TestCase):
    def test_flash_log_incomplete_ui_boot_is_not_product_ui(self):
        text = (FIXTURES / "incomplete-ui-boot.txt").read_text(encoding="utf-8")
        self.assertFalse(product_flash_log_ok(text, "abc123"))

    def test_flash_log_both_markers_pass(self):
        text = (FIXTURES / "product-ui-minimal.txt").read_text(encoding="utf-8")
        self.assertTrue(product_flash_log_ok(text, "abc123"))

    def test_build_report_records_compile_baselines(self):
        defaults = DEFAULTS.read_text(encoding="utf-8")
        report = device_build_report("abc", defaults, defaults, 100, "", "hash")
        self.assertEqual(report["document"], "family-link.device-build-report/v1")
        self.assertEqual(report["claim_class"], "compile")
        self.assertEqual(report["build_id"], "abc")
        self.assertEqual(report["sdkconfig_sha256"], "hash")
        self.assertEqual(report["elf_size_bytes"], 100)
        self.assertEqual(report["sdkconfig_baselines"]["CONFIG_LV_MEM_SIZE"], "32768")
        self.assertEqual(report["sdkconfig_baselines"]["CONFIG_ESP_MAIN_TASK_STACK_SIZE"], "16384")
        self.assertEqual(report["sdkconfig_baselines"]["CONFIG_BSP_LCD_DRAW_BUF_HEIGHT"], "40")

    def test_incomplete_ui_boot_fails_product_gate(self):
        text = (FIXTURES / "incomplete-ui-boot.txt").read_text(encoding="utf-8")
        gate = verify_product_boot(text, require_product_ui=True)
        self.assertTrue(any("product_ui_ready" in item for item in gate))
        self.assertGreater(len(verify_incomplete_ui_boot_should_fail(text)), 0)

    def test_product_ui_minimal_passes_boot_gate(self):
        text = (FIXTURES / "product-ui-minimal.txt").read_text(encoding="utf-8")
        self.assertEqual(verify_product_boot(text), [])


class DeviceGuardrailsPhase2Tests(unittest.TestCase):
    def test_lab16_quiescence_fault_injection(self):
        self.assertGreater(len(inject_lab16_fault()), 0)

    def test_product_teardown_fault_injection(self):
        self.assertGreater(len(inject_product_fault()), 0)
        self.assertEqual(inject_product_quiesced(), [])
        self.assertEqual(inject_lab16_quiesced(), [])

    def test_lab16_live_stack_job_waits_for_done(self):
        self.assertEqual(lab16_live_stack_job_errors(), [])

    def test_lab16_stack_job_without_wait_fails(self):
        sample = """
        void overlap(void) {
            lab16_upload_job_t job = {0};
            lab16_upload_job_start(&job);
        }
        """
        self.assertGreater(len(lab16_stack_job_errors(sample)), 0)

    def test_record_entry_releases_split_pane_and_present_rebuilds_it(self):
        """Second red frees the split pane before the record surface attaches; leaving RECORD rebuilds it."""
        router = (ROOT / "client/device/main/product_stage_router.cpp").read_text(encoding="utf-8")
        apply = router[router.find("static int apply_send_stage(") : router.find("extern \"C\" product_screen_t")]
        self.assertLess(apply.find("product_split_pane_release();"), apply.find("product_record_ensure("))
        self.assertGreater(apply.find("product_split_pane_release();"), 0)
        start = router.find("extern \"C\" void product_stage_present(void)")
        present = router[start : router.find("\n}\n", start)]
        self.assertLess(present.find("product_split_pane_rebuild();"), present.find("product_login_present("))
        self.assertIn("screen != PRODUCT_SCREEN_RECORD", present)

    def test_sign_out_releases_signed_in_widgets_but_keeps_takes(self):
        """Unmute rebuilds login from the same LVGL pool; a finished take must survive (DEC-033)."""
        login = (ROOT / "client/device/main/product_login_bridge.cpp").read_text(encoding="utf-8")
        start = login.find("static void enter_muted(")
        body = login[start : login.find("\n}\n", start)]
        self.assertGreater(start, 0)
        self.assertIn("product_inbox_on_mute();", body)
        self.assertIn("product_send_release_widgets();", body)
        self.assertIn("product_record_release_surface();", body)
        inbox = (ROOT / "client/device/main/product_inbox_bridge.cpp").read_text(encoding="utf-8")
        mute = inbox[inbox.find("void product_inbox_on_mute(void)") : inbox.find("void product_inbox_note_refresh")]
        self.assertIn("destroy_all_cards();", mute)
        record = (ROOT / "client/device/main/product_record_bridge.cpp").read_text(encoding="utf-8")
        start = record.find("void product_record_release_surface(void)")
        release = record[start : record.find("\n}\n", start)]
        self.assertGreater(start, 0)
        self.assertNotIn("s_record", release)
        self.assertNotIn("discard", release)

    def test_signed_in_mute_does_not_stop_inbox_worker(self):
        """Mute runs on the LVGL timer. Stopping the inbox task there can stall it."""
        text = (ROOT / "client/device/main/product_inbox_bridge.cpp").read_text(encoding="utf-8")
        start = text.find("void product_inbox_on_mute(void)")
        end = text.find("void product_inbox_note_refresh", start)
        self.assertGreater(start, 0)
        self.assertGreater(end, start)
        body = text[start:end]
        self.assertNotIn("product_inbox_http_worker_stop", body)
        self.assertNotIn("vTaskDelete", body)
        login = (ROOT / "client/device/main/product_login_bridge.cpp").read_text(encoding="utf-8")
        timer = login.find("void on_login_timer")
        timer_end = login.find("fl_product_login::SessionWriter *product_login_session", timer)
        tick = login[timer:timer_end]
        poll = tick.find("product_hardware_buttons_poll")
        http = tick.find("product_login_http_service")
        self.assertGreaterEqual(poll, 0)
        self.assertGreater(http, poll)
        self.assertIn("product_inbox_discard_results()", tick)
        self.assertLess(poll, tick.find("product_inbox_discard_results()"))

    def test_mute_latch_prefers_gpio_level(self):
        """Physical latch on BOX-3 is read from GPIO when configured."""
        text = (ROOT / "client/device/main/product_hardware_buttons.c").read_text(
            encoding="utf-8"
        )
        start = text.find("int product_hardware_buttons_mute_latched(void)")
        end = text.find("void product_hardware_buttons_poll", start)
        self.assertGreater(start, 0)
        self.assertGreater(end, start)
        body = text[start:end]
        self.assertIn("s_mute_gpio_ready", body)
        self.assertIn("gpio_get_level(BSP_BUTTON_MUTE_IO)", body)
        gpio_at = body.find("gpio_get_level(BSP_BUTTON_MUTE_IO)")
        button_at = body.find("iot_button_get_key_level")
        self.assertGreater(gpio_at, 0)
        if button_at >= 0:
            self.assertLess(gpio_at, button_at)
        init = text.find("bool product_hardware_buttons_init(void)")
        init_end = text.find("int product_hardware_buttons_mute_latched", init)
        init_body = text[init:init_end]
        self.assertIn("s_mute_gpio_ready = true", init_body)
        self.assertIn("BSP_BUTTON_MUTE_IO", init_body)

    def test_mute_display_wake_and_blank_helpers(self):
        """Wake restores duty without BSP exit_sleep (BOX-3 touch INT on GPIO 3)."""
        login = pres.load_sources()[pres.LOGIN]
        wake = pres.function_body(login, "wake_login_display")
        self.assertIn("bsp_display_brightness_set(80)", wake)
        self.assertIn("s_backlight_dark = false", wake)
        blank = pres.function_body(login, "blank_login_display")
        self.assertIn("bsp_display_backlight_off", blank)
        self.assertIn("s_backlight_dark = true", blank)

    def test_mute_clears_hangout_until_next_roster(self):
        text = (ROOT / "client/shared/login_session/fl_product_login/session.cpp").read_text(
            encoding="utf-8"
        )
        start = text.find("void SessionWriter::mute(int64_t now_ms)")
        end = text.find("void SessionWriter::unmute(int64_t now_ms)", start)
        self.assertGreater(start, 0)
        body = text[start:end]
        self.assertIn("hangout_known_ = false", body)
        self.assertIn("user_count_ = 0", body)

    def test_mute_diag_edge_logging(self):
        login = (ROOT / "client/device/main/product_login_bridge.cpp").read_text(
            encoding="utf-8"
        )
        hw = (ROOT / "client/device/main/product_hardware_buttons.c").read_text(encoding="utf-8")
        self.assertIn("V2_MUTE_EDGE", login)
        self.assertIn("product_hardware_buttons_mute_gpio_level", hw)
        self.assertIn("FAMILY_DEVICE_MUTE_TRACE", (ROOT / "client/device/main/Kconfig.projbuild").read_text())

    def test_unmute_resets_login_http_roster_fence(self):
        login = pres.load_sources()[pres.LOGIN]
        worker = (ROOT / "client/device/main/product_login_http_worker.cpp").read_text(
            encoding="utf-8"
        )
        leave = pres.function_body(login, "leave_muted")
        self.assertIn("product_login_http_on_mute()", leave)
        self.assertLess(leave.find("product_login_http_on_mute()"), leave.find("s_session.unmute(now_ms)"))
        self.assertIn("begin_roster_request", leave)
        self.assertIn("product_login_http_service(&s_session", leave)
        self.assertIn("void product_login_http_on_mute(void)", worker)
        self.assertIn("s_active_roster_id = 0", worker)

    def test_product_worker_stops_still_lack_ack(self):
        """LIFE-QUIESCE is open. This program does not rewrite the workers."""
        errors = product_stop_without_ack()
        self.assertTrue(any("product_send_http_worker.cpp" in item for item in errors))
        self.assertTrue(any("product_inbox_http_worker.cpp" in item for item in errors))
        self.assertTrue(any("product_login_http_worker.cpp" in item for item in errors))


class DeviceGuardrailsPhase3Tests(unittest.TestCase):
    def test_desk_script_on_saved_log_non_strict(self):
        text = (FIXTURES / "product-ui-minimal.txt").read_text(encoding="utf-8")
        self.assertEqual(verify_record_send_desk(text, strict_wiring=False), [])

    def test_desk_script_strict_wiring_fails_until_repair(self):
        text = (FIXTURES / "product-ui-minimal.txt").read_text(encoding="utf-8")
        errors = verify_record_send_desk(text, strict_wiring=True)
        self.assertTrue(any("INT-WIRE" in item for item in errors))


_LATCH_CALL = "product_hardware_buttons_mute_latched()"
_LIGHT_CALLS = ("bsp_display_brightness_set", "bsp_display_backlight_on")


def _strip_c_comments(text: str) -> str:
    """Drop // and /* */ comments. Strings are kept so log text cannot hide a call."""
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("//", i):
            nl = text.find("\n", i)
            if nl < 0:
                break
            out.append("\n")
            i = nl + 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                raise ValueError("unterminated block comment")
            out.append("\n" * text.count("\n", i, end))
            i = end + 2
            continue
        if text[i] == '"':
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    j += 1
                    break
                j += 1
            out.append(text[i:j])
            i = j
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def _match_closer(text: str, open_index: int, open_ch: str, close_ch: str) -> int:
    depth = 0
    i = open_index
    while i < len(text):
        if text[i] == open_ch:
            depth += 1
        elif text[i] == close_ch:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError(f"unbalanced {open_ch}")


def _skip_ws(text: str, i: int) -> int:
    while i < len(text) and text[i].isspace():
        i += 1
    return i


def _keyword_at(text: str, i: int, word: str) -> bool:
    if not text.startswith(word, i):
        return False
    before = text[i - 1] if i else " "
    after_i = i + len(word)
    after = text[after_i] if after_i < len(text) else " "
    if before == "#" or before.isalnum() or before == "_":
        return False
    if after.isalnum() or after == "_":
        return False
    return True


def _app_main_body(source: str) -> str:
    text = _strip_c_comments(source)
    marker = text.find("void app_main(void)")
    if marker < 0:
        raise AssertionError("void app_main(void) missing")
    brace = text.find("{", marker)
    end = _match_closer(text, brace, "{", "}")
    return text[brace + 1 : end]


def _latch_polarity(condition: str) -> str | None:
    compact = "".join(condition.split())
    if compact == f"!{_LATCH_CALL}":
        return "unlatched"
    if compact == _LATCH_CALL:
        return "latched"
    return None


def _parse_if_at(body: str, i: int) -> tuple[int, str, str, str | None] | None:
    """Return end index, condition, then-body, and else-body for a braced if."""
    if not _keyword_at(body, i, "if"):
        return None
    paren = _skip_ws(body, i + 2)
    if paren >= len(body) or body[paren] != "(":
        return None
    cond_end = _match_closer(body, paren, "(", ")")
    condition = body[paren + 1 : cond_end]
    brace = _skip_ws(body, cond_end + 1)
    if brace >= len(body) or body[brace] != "{":
        return None
    then_end = _match_closer(body, brace, "{", "}")
    then_body = body[brace + 1 : then_end]
    end = then_end + 1
    else_body = None
    else_at = _skip_ws(body, end)
    if _keyword_at(body, else_at, "else"):
        branch = _skip_ws(body, else_at + 4)
        if branch < len(body) and body[branch] == "{":
            else_end = _match_closer(body, branch, "{", "}")
            else_body = body[branch + 1 : else_end]
            end = else_end + 1
        elif _keyword_at(body, branch, "if"):
            nested = _parse_if_at(body, branch)
            if nested is None:
                return None
            else_body = body[branch:nested[0]]
            end = nested[0]
        else:
            return None
    return end, condition, then_body, else_body


class _LatchDecision:
    def __init__(self, start: int, end: int, latched: str | None, unlatched: str | None) -> None:
        self.start = start
        self.end = end
        self.latched = latched
        self.unlatched = unlatched


def _first_latch_decision(body: str) -> _LatchDecision | None:
    i = 0
    while i < len(body):
        if _keyword_at(body, i, "if"):
            parsed = _parse_if_at(body, i)
            if parsed is not None:
                end, condition, then_body, else_body = parsed
                polarity = _latch_polarity(condition)
                if polarity == "latched":
                    return _LatchDecision(i, end, then_body, else_body)
                if polarity == "unlatched":
                    return _LatchDecision(i, end, else_body, then_body)
            i += 2
            continue
        i += 1
    return None


def _unguarded_light_calls(body: str) -> list[str]:
    """brightness_set and backlight_on are legal only in an unlatched arm."""
    found: list[str] = []

    def scan(region: str, allowed: bool) -> None:
        i = 0
        while i < len(region):
            if _keyword_at(region, i, "if"):
                parsed = _parse_if_at(region, i)
                if parsed is None:
                    i += 2
                    continue
                end, condition, then_body, else_body = parsed
                polarity = _latch_polarity(condition)
                if polarity == "unlatched":
                    then_allowed, else_allowed = True, allowed
                elif polarity == "latched":
                    then_allowed, else_allowed = allowed, True
                else:
                    then_allowed = else_allowed = allowed
                scan(then_body, then_allowed)
                if else_body is not None:
                    scan(else_body, else_allowed)
                i = end
                continue
            matched = False
            for name in _LIGHT_CALLS:
                if _keyword_at(region, i, name):
                    if not allowed:
                        found.append(region[i:].splitlines()[0].strip())
                    i += len(name)
                    matched = True
                    break
            if not matched:
                i += 1

    scan(body, False)
    return found


_GOOD_TICK = """
static void on_login_timer(lv_timer_t *timer) {
    product_hardware_buttons_poll();
    flush_wifi_schedule();
    product_login_http_service(&s_session, now_ms);
    note_sign_in_edge();
    if (s_session.signed_in() != 0) {
        product_inbox_service(now_ms);
    } else {
        product_inbox_discard_results();
    }
    product_send_service(now_ms);
    product_record_service(now_ms);
    product_stage_present();
}
"""


class DeviceGuardrailsPresentationInvariantTests(unittest.TestCase):
    """docs/standards/device-presentation-invariants.md, one test per invariant."""

    def test_presentation_invariants_pass_on_live_firmware(self):
        self.assertEqual(pres.scan_presentation_invariants(), [])

    def test_presentation_single_timer(self):
        good = {pres.LOGIN: "lv_timer_create(on_login_timer, 50, nullptr);"}
        self.assertEqual(pres.scan_single_timer(good), [])
        second = dict(good, **{"product_inbox_bridge.cpp": "s_timer = lv_timer_create(on_inbox_timer, 50, 0);"})
        self.assertTrue(pres.scan_single_timer(second))

    def test_presentation_tick_phase_order(self):
        self.assertEqual(pres.scan_tick_phase_order(_GOOD_TICK), [])
        early = _GOOD_TICK.replace(
            "flush_wifi_schedule();", "if (s_backlight_dark) { return; }\n    flush_wifi_schedule();"
        )
        self.assertTrue(any("PRES-NORETURN" in e for e in pres.scan_tick_phase_order(early)))
        late_poll = _GOOD_TICK.replace("    product_hardware_buttons_poll();\n", "").replace(
            "product_stage_present();", "product_stage_present();\n    product_hardware_buttons_poll();"
        )
        self.assertTrue(pres.scan_tick_phase_order(late_poll))
        render_after = _GOOD_TICK.replace(
            "product_stage_present();", "product_stage_present();\n    render_login_copy();"
        )
        self.assertTrue(pres.scan_tick_phase_order(render_after))

    def test_presentation_single_visibility_owner(self):
        router = "extern \"C\" void product_stage_present(void) { product_inbox_set_ui_visible(1); }"
        self.assertEqual(pres.scan_single_visibility_owner({pres.ROUTER: router}), [])
        cross = {pres.ROUTER: router, "product_send_bridge.cpp": "static void show(void) { product_inbox_set_ui_visible(0); }"}
        self.assertTrue(pres.scan_single_visibility_owner(cross))
        stray = {pres.ROUTER: router + " void other(void) { product_volume_set_visible(0); }"}
        self.assertTrue(pres.scan_single_visibility_owner(stray))

    def test_presentation_no_z_order_games(self):
        self.assertEqual(pres.scan_no_z_order({"a.cpp": "lv_obj_move_to_index(card, 0);"}), [])
        self.assertTrue(pres.scan_no_z_order({"a.cpp": "lv_obj_move_foreground(s_roster_row);"}))
        self.assertTrue(pres.scan_no_z_order({"a.cpp": "lv_obj_move_background(s_lower_root);"}))

    def test_presentation_single_pointer_reset(self):
        wake = "static void wake_login_display(void) { lv_indev_reset(indev, nullptr); }"
        self.assertEqual(pres.scan_single_pointer_reset({pres.LOGIN: wake}), [])
        per_tick = wake + " static void render_pin_screen(void) { lv_indev_reset(indev, nullptr); }"
        self.assertTrue(pres.scan_single_pointer_reset({pres.LOGIN: per_tick}))

    def test_presentation_display_power(self):
        login = (
            "static void wake_login_display(void) { (void)bsp_display_brightness_set(80); }\n"
            "static void blank_login_display(void) { (void)bsp_display_backlight_off(); }\n"
        )
        self.assertEqual(pres.scan_display_power({pres.LOGIN: login}), [])
        sleep = login + "static void x(void) { bsp_display_exit_sleep(); }"
        self.assertTrue(pres.scan_display_power({pres.LOGIN: sleep}))
        stray = login + "static void render_login_copy(void) { (void)bsp_display_brightness_set(80); }"
        self.assertTrue(pres.scan_display_power({pres.LOGIN: stray}))

    def test_presentation_mute_reconcile(self):
        hw = "void product_hardware_buttons_poll(void) { product_login_on_mute_level(product_hardware_buttons_mute_latched()); }"
        login = """
        static void enter_muted(int64_t now_ms) { product_login_http_on_mute(); }
        static void leave_muted(int64_t now_ms) { product_login_http_on_mute(); }
        extern "C" void product_login_on_mute_level(int latched) {
            if (latched) { enter_muted(now_ms); } else { leave_muted(now_ms); }
        }
        """
        self.assertEqual(pres.scan_mute_reconcile(login, hw), [])
        renders = login.replace("leave_muted(now_ms); }\n", "leave_muted(now_ms); }\n render_login_copy();\n", 1)
        self.assertTrue(any("must not call render_" in e for e in pres.scan_mute_reconcile(renders, hw)))
        resets = login.replace(
            "static void leave_muted(int64_t now_ms) {",
            "static void leave_muted(int64_t now_ms) { lv_indev_reset(indev, nullptr);",
        )
        self.assertTrue(pres.scan_mute_reconcile(resets, hw))
        edge_only = "void product_hardware_buttons_poll(void) { if (pressed) { product_login_on_mute_pressed(); } }"
        self.assertTrue(pres.scan_mute_reconcile(login, edge_only))

    def test_presentation_render_is_pure(self):
        login = (
            "static void render_pin_screen(void) { set_label_text(s_upper_label, heading); }\n"
            "static void render_login_copy(void) { render_pin_screen(); }\n" + _GOOD_TICK
        )
        self.assertEqual(pres.scan_render_is_pure(login), [])
        transition = login.replace(
            "{ render_pin_screen(); }", "{ product_shell_set_stage(FL_SHELL_STAGE_INBOX); render_pin_screen(); }"
        )
        self.assertTrue(pres.scan_render_is_pure(transition))
        realign = login.replace(
            "set_label_text(s_upper_label, heading);",
            "set_label_text(s_upper_label, heading); lv_obj_align(s_upper_label, LV_ALIGN_LEFT_MID, 12, 0);",
        )
        self.assertTrue(pres.scan_render_is_pure(realign))
        cross = login.replace("{ render_pin_screen(); }", "{ product_inbox_on_signed_in(); render_pin_screen(); }")
        self.assertTrue(pres.scan_render_is_pure(cross))


class DeviceGuardrailsMuteBootTests(unittest.TestCase):
    def test_latched_boot_never_sets_backlight_duty(self):
        """A latched boot must not set a non-zero backlight duty in app_main.

        The mute latch is read first. Only the unlatched arm may call
        brightness_set or backlight_on. The latched arm blanks before the
        shell and login attach failure returns.
        """
        app_main_src = (ROOT / "client/device/main/app_main.c").read_text(encoding="utf-8")
        app_main_src = _strip_c_comments(app_main_src)
        boot_backlight = pres.function_body(app_main_src, "boot_backlight_after_display")
        decision = _first_latch_decision(boot_backlight)
        self.assertIsNotNone(decision)
        assert decision is not None
        before = boot_backlight[: decision.start]
        self.assertNotIn("bsp_display_brightness_set", before)
        self.assertNotIn("bsp_display_backlight_on", before)
        self.assertIsNotNone(decision.latched)
        assert decision.latched is not None
        self.assertIn("bsp_display_backlight_off(", decision.latched)
        self.assertNotIn("bsp_display_brightness_set", decision.latched)
        self.assertNotIn("bsp_display_backlight_on", decision.latched)
        if decision.unlatched is not None:
            unlatched_arm = decision.unlatched
        else:
            unlatched_arm = boot_backlight[decision.end :]
        self.assertIn("bsp_display_brightness_set(80)", unlatched_arm)
        self.assertIn("bsp_display_backlight_on(", unlatched_arm)

        app_start = (ROOT / "client/device/main/product_app_start.c").read_text(encoding="utf-8")
        app_start = _strip_c_comments(app_start)
        run_body = pres.function_body(app_start, "product_app_run")
        for marker in (
            "lvgl_lock_failed",
            "product_shell_init_failed",
            "product_login_attach_failed",
        ):
            self.assertIn(marker, run_body)
        self.assertEqual(_unguarded_light_calls(run_body), [])


if __name__ == "__main__":
    unittest.main()
