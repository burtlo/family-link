#!/usr/bin/env python3
"""L15-02: declared peak allocation sums match README resource budget."""
from __future__ import annotations

import unittest

PCM_RING_BYTES = 5_760_000
SKETCH_BYTES_MAX = 32_768
CANVAS_BYTES_PER_BUFFER = 115_200
CANVAS_BUFFER_COUNT = 2
LVGL_MEM_PLANNING_BYTES = 65_536
MAX_TRANSIENT_BYTES = 6_500_000
MAX_PERSISTENT_BYTES = 250_000


class Lab15AllocationMathTests(unittest.TestCase):
    def test_pcm_ring_size(self) -> None:
        self.assertEqual(PCM_RING_BYTES, 180 * 1000 * 16 * 2)

    def test_declared_capture_peaks_sum_within_transient_budget(self) -> None:
        canvas_retained = CANVAS_BYTES_PER_BUFFER * CANVAS_BUFFER_COUNT
        capture_peak = PCM_RING_BYTES + SKETCH_BYTES_MAX + canvas_retained
        self.assertLessEqual(capture_peak, MAX_TRANSIENT_BYTES)
        self.assertLessEqual(canvas_retained, MAX_PERSISTENT_BYTES)

    def test_planning_checklist(self) -> None:
        declared_next = PCM_RING_BYTES + SKETCH_BYTES_MAX + (
            CANVAS_BYTES_PER_BUFFER * CANVAS_BUFFER_COUNT
        )
        self.assertGreater(declared_next, PCM_RING_BYTES)
        self.assertEqual(LVGL_MEM_PLANNING_BYTES, 65536)


if __name__ == "__main__":
    unittest.main()
