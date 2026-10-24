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
from device_guardrails.product_boot_serial import verify_bootstrap_only_should_fail, verify_product_boot
from device_guardrails.quiescence_fault import (
    inject_lab16_fault,
    inject_lab16_quiesced,
    inject_product_fault,
    inject_product_quiesced,
    lab16_live_stack_job_errors,
    lab16_stack_job_errors,
    product_stop_without_ack,
)
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

    def test_live_timer_path_reports_open_sd_mount_only(self):
        """PRES-PATH is open on live firmware. The scan must keep seeing it.

        Send repair owns moving bsp_sdcard_mount off this path. After that
        move, this test should expect an empty list.
        """
        errors = scan_timer_path()
        self.assertEqual(
            [item for item in errors if "bsp_sdcard_mount" not in item],
            [],
            errors,
        )
        self.assertTrue(any("ensure_media" in item and "bsp_sdcard_mount" in item for item in errors))

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
    def test_flash_log_bootstrap_only_is_not_product_ui(self):
        text = (FIXTURES / "bootstrap-only.txt").read_text(encoding="utf-8")
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

    def test_bootstrap_only_fails_product_gate(self):
        text = (FIXTURES / "bootstrap-only.txt").read_text(encoding="utf-8")
        gate = verify_product_boot(text, require_product_ui=True)
        self.assertTrue(any("product_ui_ready" in item for item in gate))
        self.assertGreater(len(verify_bootstrap_only_should_fail(text)), 0)

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


class DeviceGuardrailsMuteBootTests(unittest.TestCase):
    def test_latched_boot_never_sets_backlight_duty(self):
        """A latched boot must not set a non-zero backlight duty in app_main.

        The mute latch is read first. Only the unlatched arm may call
        brightness_set or backlight_on. The latched arm blanks before the
        shell and login attach failure returns.
        """
        source = (ROOT / "client/device/main/app_main.c").read_text(encoding="utf-8")
        body = _app_main_body(source)
        decision = _first_latch_decision(body)
        self.assertIsNotNone(decision)
        assert decision is not None
        before = body[: decision.start]
        self.assertNotIn("bsp_display_brightness_set", before)
        self.assertNotIn("bsp_display_backlight_on", before)
        self.assertIsNotNone(decision.latched)
        self.assertIsNotNone(decision.unlatched)
        assert decision.latched is not None and decision.unlatched is not None
        self.assertIn("bsp_display_backlight_off(", decision.latched)
        self.assertNotIn("bsp_display_brightness_set", decision.latched)
        self.assertNotIn("bsp_display_backlight_on", decision.latched)
        self.assertIn("bsp_display_brightness_set(80)", decision.unlatched)
        self.assertIn("bsp_display_backlight_on(", decision.unlatched)
        for marker in (
            "lvgl_lock_failed",
            "product_shell_init_failed",
            "product_login_attach_failed",
        ):
            self.assertLess(decision.end, body.index(marker), marker)
        self.assertEqual(_unguarded_light_calls(body), [])


if __name__ == "__main__":
    unittest.main()
