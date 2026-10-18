#!/usr/bin/env python3
"""Keep the LVGL canvas address-alignment contract explicit on the host."""

import re
import unittest
from pathlib import Path


PACKAGE = Path(__file__).resolve().parents[1]
SOURCE = (PACKAGE / "main/lab10_ui_lvgl.c").read_text(encoding="utf-8")
SDKCONFIG = (PACKAGE / "sdkconfig").read_text(encoding="utf-8")


class CanvasAlignmentContractTest(unittest.TestCase):
    def test_configured_lvgl_address_alignment_is_not_stride_alignment(self):
        self.assertRegex(SDKCONFIG, r"(?m)^CONFIG_LV_DRAW_BUF_ALIGN=4$")
        self.assertRegex(SDKCONFIG, r"(?m)^CONFIG_LV_DRAW_BUF_STRIDE_ALIGN=1$")
        alignment_body = re.search(
            r"static bool canvas_buffer_is_aligned\(.*?\n}\n", SOURCE, re.S
        )
        self.assertIsNotNone(alignment_body)
        self.assertIn("LAB10_CANVAS_BUFFER_ALIGNMENT", alignment_body.group(0))
        self.assertNotIn("LV_DRAW_BUF_STRIDE_ALIGN", alignment_body.group(0))

    def test_allocation_requests_the_same_alignment_it_validates(self):
        self.assertIn(
            "#define LAB10_CANVAS_BUFFER_ALIGNMENT LV_DRAW_BUF_ALIGN", SOURCE
        )
        self.assertRegex(
            SOURCE,
            r"heap_caps_aligned_alloc\(\s*"
            r"LAB10_CANVAS_BUFFER_ALIGNMENT,\s*bytes,\s*caps\)",
        )
        self.assertIn("canvas_buffer_is_aligned(buffer)", SOURCE)
        self.assertIn("heap_caps_free(buffer)", SOURCE)


if __name__ == "__main__":
    unittest.main()
