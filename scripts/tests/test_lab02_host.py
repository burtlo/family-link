"""L1 host checks for LAB-02 fixtures and snap math (parity with snap_math.c)."""
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "client/labs/lab-02-bounded-inbox/fixtures"
LAB02_LAYOUT = {
    "LCD_W": 320,
    "CARD_W": 132,
    "CARD_GAP": 12,
}


def _stride():
    return LAB02_LAYOUT["CARD_W"] + LAB02_LAYOUT["CARD_GAP"]


def snap_target_x_for_index(index: int, item_count: int) -> int:
    """Mirror of lab02_snap_target_x_for_index in snap_math.c."""
    if item_count <= 0 or index < 0:
        return 0
    if index >= item_count:
        index = item_count - 1
    card_center = LAB02_LAYOUT["CARD_GAP"] + index * _stride() + LAB02_LAYOUT["CARD_W"] // 2
    view_w = LAB02_LAYOUT["LCD_W"]
    target = card_center - view_w // 2
    content_w = LAB02_LAYOUT["CARD_GAP"] + item_count * _stride()
    max_x = max(0, content_w - view_w)
    return max(0, min(target, max_x))


def snap_index_from_scroll_x(scroll_x: int, item_count: int) -> int:
    """Mirror of lab02_snap_index_from_scroll_x in snap_math.c."""
    if item_count <= 0:
        return 0
    mid = scroll_x + LAB02_LAYOUT["LCD_W"] // 2
    rel = mid - LAB02_LAYOUT["CARD_GAP"] - LAB02_LAYOUT["CARD_W"] // 2
    idx = rel // _stride()
    return max(0, min(idx, item_count - 1))


class Lab02FixtureTests(unittest.TestCase):
    def test_fixture_counts(self):
        for name, expected in (("inbox_8", 8), ("inbox_16", 16), ("inbox_64", 64), ("inbox_256", 256)):
            path = FIXTURES / f"{name}.json"
            self.assertTrue(path.is_file(), path)
            data = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(data.get("schema"), "inbox-summary/v1")
            self.assertEqual(len(data["items"]), expected)
            self.assertEqual(data["items"][0]["message_id"], "msg-0000")

    def test_page_size_default_16(self):
        data = json.loads((FIXTURES / "inbox_256.json").read_text(encoding="utf-8"))
        self.assertEqual(data.get("page_size"), 16)


class Lab02SnapMathTests(unittest.TestCase):
    def test_first_and_last_clamped(self):
        n = 16
        self.assertEqual(snap_target_x_for_index(0, n), 0)
        last = snap_target_x_for_index(n - 1, n)
        self.assertGreater(last, 0)

    def test_round_trip_mid_index(self):
        n = 64
        for idx in (0, 7, 31, 62):
            target = snap_target_x_for_index(idx, n)
            got = snap_index_from_scroll_x(target, n)
            self.assertEqual(got, idx, f"index {idx} target {target}")

    def test_last_index_uses_max_scroll_clamp(self):
        n = 64
        last = n - 1
        target = snap_target_x_for_index(last, n)
        self.assertEqual(snap_index_from_scroll_x(target, n), last - 1)

    def test_large_inbox_metadata_count(self):
        n = 256
        target = snap_target_x_for_index(128, n)
        self.assertEqual(snap_index_from_scroll_x(target, n), 128)


if __name__ == "__main__":
    unittest.main()
