"""Real pinned LVGL host tests for client/device controller lifecycle and capture."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class ViewControllerTests(unittest.TestCase):
    def test_message_target_has_no_media_side_effects(self):
        service = (ROOT / "client/device/main/product_message_target_service.cpp").read_text()
        controller = (ROOT / "client/device/main/product_message_target_view_controller.cpp").read_text()
        self.assertNotIn("lv_obj", service)
        for token in ("heap_caps_malloc", "product_send", "product_record", "product_audio",
                      "microphone", "esp_http_client", "lv_canvas", "timer_create"):
            self.assertNotIn(token, service + controller)
        self.assertIn("MessageTargetDestination::Recording", controller)

    def test_snapshot_capture_uses_caller_buffer(self):
        source = (ROOT / "client/device/main/product_transition_coordinator.cpp").read_text()
        self.assertIn("lv_snapshot_take_to_draw_buf", source)
        self.assertNotIn("lv_snapshot_take(", source)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", source)

    def test_login_boundaries(self):
        service = (ROOT / "client/device/main/product_login_service.cpp").read_text()
        controller = (ROOT / "client/device/main/product_login_view_controller.cpp").read_text()
        self.assertNotIn("lvgl", service.lower())
        self.assertNotIn("lv_obj", service)
        self.assertNotIn("select_pin_user", controller)
        for token in ("product_inbox", "product_send", "product_volume", "product_record"):
            self.assertNotIn(token, service + controller)

    def test_real_lvgl_lifecycle_capture_and_failures(self):
        config = (ROOT / "client/device/sdkconfig.defaults").read_text()
        self.assertIn("CONFIG_LV_MEM_SIZE=32768", config)
        with tempfile.TemporaryDirectory(prefix="view-controller-") as build:
            for command in (["cmake", "-S", str(ROOT / "scripts/tests/view_controller_host"),
                             "-B", build, "-DCMAKE_BUILD_TYPE=Debug"],
                            ["cmake", "--build", build, "-j", "8"]):
                result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT)
                self.assertEqual(result.returncode, 0, result.stdout)
            subprocess.run([str(pathlib.Path(build) / "view_controller_test")], check=True,
                           cwd=ROOT)
