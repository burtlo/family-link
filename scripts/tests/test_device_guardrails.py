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

    def test_ui_window_owns_only_screen_and_content_root(self):
        """P4: UiWindow has no scene, split-pane, or feature knowledge; the router never mounts scenes."""
        for name in ("product_ui_window.hpp", "product_ui_window.cpp"):
            text = pres.strip_comments((ROOT / "client/device/main" / name).read_text(encoding="utf-8"))
            for forbidden in ("mountDesk", "mountRecord", "deskRoots", "Scene", "stage_shell", "product_login",
                              "product_inbox", "product_send", "product_volume", "product_record"):
                self.assertNotIn(forbidden, text, f"{name} still references {forbidden}")
        router = (ROOT / "client/device/main/product_stage_router.cpp").read_text(encoding="utf-8")
        self.assertNotIn("uiWindow()", router)

    def test_boot_installs_root_login_runtime_before_ready(self):
        """P4: boot pushes Login under Root and starts the runtime before product_ui_ready."""
        app_run = pres.strip_comments(
            (ROOT / "client/device/main/product_app_run.cpp").read_text(encoding="utf-8")
        )
        attach = pres.function_body(app_run, "attach_product_ui")
        order = [attach.find(token) for token in (
            "win.create(", "RootViewController(", "ProductRuntime(", "LoginViewController(", "s_runtime->start()",
        )]
        self.assertTrue(all(i >= 0 for i in order), order)
        self.assertEqual(order, sorted(order))
        self.assertNotIn("mountDesk", app_run)
        run = pres.function_body(app_run, "run")
        self.assertLess(run.find("attach_product_ui()"), run.find("product_ui_ready"))
        self.assertLess(run.find("product_ui_ready"), run.find("product_network::start()"))

    def test_sign_out_resets_login_http_and_session(self):
        """D-2: mute signs the Login session out and fences in-flight roster HTTP."""
        service = pres.strip_comments(
            (ROOT / "client/device/main/product_login_service.cpp").read_text(encoding="utf-8")
        )
        sign_out = pres.function_body(service, "signOut")
        self.assertLess(sign_out.find("transport_.mute()"), sign_out.find("session_.mute(now)"))

    def test_signed_in_mute_does_not_stop_inbox_worker(self):
        """Mute runs on the LVGL timer. Stopping the inbox task there can stall it."""
        text = (ROOT / "client/device/main/product_inbox_bridge.cpp").read_text(encoding="utf-8")
        start = text.find("void on_mute(void)")
        end = text.find("void note_refresh", start)
        self.assertGreater(start, 0)
        self.assertGreater(end, start)
        body = text[start:end]
        self.assertNotIn("product_inbox_http_worker_stop", body)
        self.assertNotIn("vTaskDelete", body)

    def test_mute_latch_prefers_gpio_level(self):
        """Physical latch on BOX-3 is read from GPIO when configured."""
        text = (ROOT / "client/device/main/product_hardware_buttons.cpp").read_text(
            encoding="utf-8"
        )
        start = text.find("int mute_latched()")
        end = text.find("void poll", start)
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
        init = text.find("bool init()")
        init_end = text.find("int mute_latched", init)
        init_body = text[init:init_end]
        self.assertIn("s_mute_gpio_ready = true", init_body)
        self.assertIn("BSP_BUTTON_MUTE_IO", init_body)

    def test_mute_display_wake_and_blank_helpers(self):
        """Wake restores duty without BSP exit_sleep (BOX-3 touch INT on GPIO 3)."""
        runtime = pres.load_sources()[pres.RUNTIME]
        wake = pres.function_body(runtime, "wake_login_display")
        self.assertIn("bsp_display_brightness_set(80)", wake)
        self.assertNotIn("exit_sleep", wake)
        blank = pres.function_body(runtime, "blank_login_display")
        self.assertIn("bsp_display_backlight_off", blank)
        self.assertNotIn("enter_sleep", blank)

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
        runtime = (ROOT / "client/device/main/product_runtime.cpp").read_text(encoding="utf-8")
        hw = (ROOT / "client/device/main/product_hardware_buttons.cpp").read_text(encoding="utf-8")
        self.assertIn("V2_MUTE_EDGE", runtime)
        self.assertIn("mute_gpio_level", hw)
        self.assertIn("FAMILY_DEVICE_MUTE_TRACE", (ROOT / "client/device/main/Kconfig.projbuild").read_text())

    def test_unmute_resets_login_http_roster_fence(self):
        service = pres.load_sources()["product_login_service.cpp"]
        worker = (ROOT / "client/device/main/product_login_http_worker.cpp").read_text(
            encoding="utf-8"
        )
        refresh = pres.function_body(service, "refreshHangout")
        self.assertLess(refresh.find("transport_.mute()"), refresh.find("session_.unmute(now)"))
        self.assertIn("product_login_http::on_mute", service)
        self.assertIn("void on_mute()", worker)
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


_LATCH_CALL = "product_buttons::mute_latched()"
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
    if compact in (f"!{_LATCH_CALL}", f"{_LATCH_CALL}==0"):
        return "unlatched"
    if compact in (_LATCH_CALL, f"{_LATCH_CALL}!=0"):
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
void ProductRuntime::tick(int64_t now) {
    pollInput(now);
    service_.flushConnectivity(now);
    service_.advance(now);
    serviceFeatures();
    present();
}
"""

_GOOD_POLL = """
void ProductRuntime::pollInput(int64_t now) {
    const bool latched = platform_.muteLatched && platform_.muteLatched() != 0;
    if (latched) {
        root_.finishTransition();
        if (platform_.blankDisplay) platform_.blankDisplay();
        service_.signOut(now);
    } else if (muted_) {
        service_.refreshHangout(now);
        root_.render();
        if (platform_.wakeDisplay) platform_.wakeDisplay();
    }
}
"""


class DeviceGuardrailsPresentationInvariantTests(unittest.TestCase):
    """docs/standards/device-presentation-invariants.md, one test per invariant."""

    def test_presentation_invariants_pass_on_live_firmware(self):
        self.assertEqual(pres.scan_presentation_invariants(), [])

    def test_presentation_single_timer(self):
        good = {pres.RUNTIME: "timer_ = lv_timer_create(on_product_timer, 50, this);"}
        self.assertEqual(pres.scan_single_timer(good), [])
        second = dict(good, **{"product_inbox_bridge.cpp": "s_timer = lv_timer_create(on_inbox_timer, 50, 0);"})
        self.assertTrue(pres.scan_single_timer(second))
        controller_owned = {"product_login_view_controller.cpp": "lv_timer_create(on_product_timer, 50, this);"}
        self.assertTrue(pres.scan_single_timer(controller_owned))

    def test_presentation_tick_phase_order(self):
        self.assertEqual(pres.scan_tick_phase_order(_GOOD_TICK), [])
        early = _GOOD_TICK.replace(
            "service_.flushConnectivity(now);", "if (muted_) { return; }\n    service_.flushConnectivity(now);"
        )
        self.assertTrue(any("PRES-NORETURN" in e for e in pres.scan_tick_phase_order(early)))
        late_poll = _GOOD_TICK.replace("    pollInput(now);\n", "").replace(
            "present();\n}", "present();\n    pollInput(now);\n}"
        )
        self.assertTrue(pres.scan_tick_phase_order(late_poll))
        render_after = _GOOD_TICK.replace("present();\n}", "present();\n    root_.render();\n}")
        self.assertTrue(pres.scan_tick_phase_order(render_after))

    def test_presentation_single_visibility_owner(self):
        router = "void present() { product_inbox::set_ui_visible(1); }"
        self.assertEqual(pres.scan_single_visibility_owner({pres.ROUTER: router}), [])
        cross = {pres.ROUTER: router, "product_send_bridge.cpp": "static void show(void) { product_inbox::set_ui_visible(0); }"}
        self.assertTrue(pres.scan_single_visibility_owner(cross))
        stray = {pres.ROUTER: router + " void other(void) { product_volume::set_visible(0); }"}
        self.assertTrue(pres.scan_single_visibility_owner(stray))

    def test_presentation_no_z_order_games(self):
        self.assertEqual(pres.scan_no_z_order({"a.cpp": "lv_obj_move_to_index(card, 0);"}), [])
        self.assertTrue(pres.scan_no_z_order({"a.cpp": "lv_obj_move_foreground(s_roster_row);"}))
        self.assertTrue(pres.scan_no_z_order({"a.cpp": "lv_obj_move_background(s_lower_root);"}))

    def test_presentation_single_pointer_reset(self):
        wake = "static void wake_login_display(void) { lv_indev_reset(indev, nullptr); }"
        self.assertEqual(pres.scan_single_pointer_reset({pres.RUNTIME: wake}), [])
        per_tick = wake + " void ProductRuntime::pollInput(int64_t now) { lv_indev_reset(indev, nullptr); }"
        self.assertTrue(pres.scan_single_pointer_reset({pres.RUNTIME: per_tick}))
        moved = {"product_login_view_controller.cpp": wake}
        self.assertTrue(pres.scan_single_pointer_reset(moved))

    def test_presentation_display_power(self):
        runtime = (
            "void wake_login_display() { (void)bsp_display_brightness_set(80); }\n"
            "void blank_login_display() { (void)bsp_display_backlight_off(); }\n"
        )
        self.assertEqual(pres.scan_display_power({pres.RUNTIME: runtime}), [])
        sleep = runtime + "void x() { bsp_display_exit_sleep(); }"
        self.assertTrue(pres.scan_display_power({pres.RUNTIME: sleep}))
        stray = {
            pres.RUNTIME: runtime,
            "product_login_view_controller.cpp": "void LoginViewController::render() { (void)bsp_display_brightness_set(80); }",
        }
        self.assertTrue(pres.scan_display_power(stray))

    def test_presentation_mute_reconcile(self):
        self.assertEqual(pres.scan_mute_reconcile(_GOOD_POLL, ""), [])
        no_sign_out = _GOOD_POLL.replace("service_.signOut(now);", "")
        self.assertTrue(pres.scan_mute_reconcile(no_sign_out, ""))
        no_finish = _GOOD_POLL.replace("root_.finishTransition();", "")
        self.assertTrue(pres.scan_mute_reconcile(no_finish, ""))
        resets = _GOOD_POLL.replace("root_.render();", "root_.render(); lv_indev_reset(indev, nullptr);")
        self.assertTrue(pres.scan_mute_reconcile(resets, ""))
        wake_first = _GOOD_POLL.replace(
            "service_.refreshHangout(now);\n        root_.render();\n        if (platform_.wakeDisplay) platform_.wakeDisplay();",
            "if (platform_.wakeDisplay) platform_.wakeDisplay();\n        service_.refreshHangout(now);",
        )
        self.assertTrue(any("before wake" in e for e in pres.scan_mute_reconcile(wake_first, "")))

    def test_presentation_render_is_pure(self):
        login = "void LoginViewController::render() { set_label_text(heading_, text); }\n"
        self.assertEqual(pres.scan_render_is_pure(login), [])
        navigates = login.replace("{ set_label", "{ root_.replace(next, true); set_label")
        self.assertTrue(pres.scan_render_is_pure(navigates))
        realign = login.replace("text); }", "text); lv_obj_align(heading_, LV_ALIGN_LEFT_MID, 12, 0); }")
        self.assertTrue(pres.scan_render_is_pure(realign))
        cross = login.replace("{ set_label", "{ product_inbox::on_signed_in(); set_label")
        self.assertTrue(pres.scan_render_is_pure(cross))
        wakes = login.replace("{ set_label", "{ wake_login_display(); set_label")
        self.assertTrue(pres.scan_render_is_pure(wakes))


class DeviceGuardrailsMuteBootTests(unittest.TestCase):
    def test_latched_boot_never_sets_backlight_duty(self):
        """A latched boot must not set a non-zero backlight duty in app_main.

        The mute latch is read first. Only the unlatched arm may call
        brightness_set or backlight_on. The latched arm blanks before the
        shell and login attach failure returns.
        """
        app_main_src = (ROOT / "client/device/main/app_main.cpp").read_text(encoding="utf-8")
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

        app_run_src = (ROOT / "client/device/main/product_app_run.cpp").read_text(encoding="utf-8")
        app_run_src = _strip_c_comments(app_run_src)
        run_body = pres.function_body(app_run_src, "run")
        attach_body = pres.function_body(app_run_src, "attach_product_ui")
        for marker in (
            "product_ui_window_create_failed",
            "product_ui_login_load_failed",
            "product_ui_runtime_start_failed",
        ):
            self.assertIn(marker, attach_body)
        self.assertIn("lvgl_lock_failed", app_run_src)
        self.assertEqual(_unguarded_light_calls(run_body), [])


if __name__ == "__main__":
    unittest.main()
