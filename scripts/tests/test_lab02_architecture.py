"""Mechanical boundaries for LAB-02 presentation and HTTP worker modules."""

from pathlib import Path
import unittest


LAB = (Path(__file__).resolve().parents[2] /
       "client/labs/lab-02-bounded-inbox/components/lab02")


class Lab02ArchitectureTests(unittest.TestCase):
    def test_presentation_does_not_wait_on_io_or_sleep(self):
        for name in ("carousel_controller.c", "card_pool.c", "card_view.c", "snap.c"):
            with self.subTest(module=name):
                source = (LAB / name).read_text(encoding="utf-8")
                self.assertNotRegex(
                    source,
                    r"\b(?:vTaskDelay|xSemaphoreTake|xQueueReceive|"
                    r"esp_http_client_\w+|lab02_board_wifi_connect)\s*\(",
                )

    def test_http_worker_has_no_presentation_dependency(self):
        source = (LAB / "page_provider_http.c").read_text(encoding="utf-8")
        self.assertNotRegex(source, r"#include\s+\"lab02/(?:carousel_controller|card_view|card_pool)\.h\"")
        self.assertNotRegex(source, r"\b(?:lv_\w+|lab02_carousel_\w+)\s*\(")

    def test_session_is_only_model_writer(self):
        source = (LAB / "page_provider_http.c").read_text(encoding="utf-8")
        self.assertNotRegex(source, r"\blab02_inbox_model_apply_v2_page\s*\(")
        self.assertNotRegex(source, r"\blab02_inbox_model_merge_v2_page\s*\(")


if __name__ == "__main__":
    unittest.main()
