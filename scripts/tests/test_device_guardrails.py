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


if __name__ == "__main__":
    unittest.main()
