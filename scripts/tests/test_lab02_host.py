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
    def test_partial_slot_without_message_id(self):
        """Materialized count ignores empty message_id slots within total_count."""
        items = [{"message_id": "m1", "sender_name": "A"}] + [{}] * 15
        materialized = 0
        total = 16
        for i in range(total):
            mid = items[i].get("message_id") if i < len(items) else None
            if mid:
                materialized = i + 1
        self.assertEqual(materialized, 1)

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


LAB02_FETCH_QUEUE_CAP = 8


class Lab02FetchQueueModel:
    """Mirror of lab02_session HTTP enqueue dedupe + capacity (host L1)."""

    def __init__(self, capacity: int = LAB02_FETCH_QUEUE_CAP):
        self.capacity = capacity
        self.queue: list[tuple[int, int]] = []
        self.inflight: dict[int, int] = {}
        self.loaded_pages: set[int] = set()
        self.overflow = 0
        self.next_op = 1

    def enqueue(self, page: int) -> bool:
        if page in self.loaded_pages:
            return False
        if page in self.inflight:
            return False
        if len(self.queue) >= self.capacity:
            self.overflow += 1
            return False
        op = self.next_op
        self.next_op += 1
        self.inflight[page] = op
        self.queue.append((page, op))
        return True

    def complete(self, page: int, op: int) -> None:
        if self.inflight.get(page) != op:
            return
        del self.inflight[page]
        self.loaded_pages.add(page)
        self.queue = [(p, o) for p, o in self.queue if p != page]


class Lab02FetchOutcomeModel:
    """Mirror terminal fetch outcomes clearing inflight (host L1)."""

    def __init__(self):
        self.inflight: dict[int, int] = {}
        self.metrics = {"fetch_fail": 0, "fetch_timeout": 0, "apply_stale": 0}

    def finish(self, page: int, op: int, outcome: str) -> None:
        if self.inflight.get(page) != op:
            self.metrics["apply_stale"] += 1
            return
        self.inflight[page] = 0
        if outcome == "fail":
            self.metrics["fetch_fail"] += 1
        elif outcome == "timeout":
            self.metrics["fetch_timeout"] += 1


class Lab02ApplyDeferModel:
    """Mirror try_apply_pending + scroll_lock unlock drain (host L1)."""

    def __init__(self):
        self.scroll_locked = False
        self.pending_merge = False
        self.applied = 0
        self.apply_deferred = 0

    def fetch_success_pending(self) -> None:
        self.pending_merge = True

    def try_apply(self) -> bool:
        if not self.pending_merge or self.scroll_locked:
            if self.pending_merge and self.scroll_locked:
                self.apply_deferred += 1
            return False
        self.pending_merge = False
        self.applied += 1
        return True

    def set_scroll_lock(self, locked: bool) -> None:
        was = self.scroll_locked
        self.scroll_locked = locked
        if was and not locked:
            self.try_apply()


class Lab02SessionQueueTests(unittest.TestCase):
    def test_queue_capacity_overflow(self):
        q = Lab02FetchQueueModel(capacity=2)
        self.assertTrue(q.enqueue(0))
        self.assertTrue(q.enqueue(1))
        self.assertFalse(q.enqueue(2))
        self.assertEqual(q.overflow, 1)

    def test_dedupe_inflight_and_loaded(self):
        q = Lab02FetchQueueModel()
        self.assertTrue(q.enqueue(3))
        self.assertFalse(q.enqueue(3))
        q.complete(3, q.inflight[3])
        self.assertFalse(q.enqueue(3))

    def test_timeout_terminal_clears_inflight(self):
        m = Lab02FetchOutcomeModel()
        m.inflight[2] = 7
        m.finish(2, 7, "timeout")
        self.assertEqual(m.inflight[2], 0)
        self.assertEqual(m.metrics["fetch_timeout"], 1)
        self.assertEqual(m.metrics["fetch_fail"], 0)

    def test_stale_op_mismatch_increments_apply_stale(self):
        m = Lab02FetchOutcomeModel()
        m.inflight[1] = 3
        m.finish(1, 99, "fail")
        self.assertEqual(m.inflight[1], 3)
        self.assertEqual(m.metrics["apply_stale"], 1)
        self.assertEqual(m.metrics["fetch_fail"], 0)

    def test_scroll_lock_defers_merge_until_unlock(self):
        d = Lab02ApplyDeferModel()
        d.scroll_locked = True
        d.fetch_success_pending()
        self.assertFalse(d.try_apply())
        self.assertEqual(d.apply_deferred, 1)
        self.assertEqual(d.applied, 0)
        d.set_scroll_lock(False)
        self.assertEqual(d.applied, 1)
        self.assertFalse(d.pending_merge)


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
