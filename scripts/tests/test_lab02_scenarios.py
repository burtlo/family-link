import sys
import threading
import unittest
from pathlib import Path
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lab02_scenarios import Lab02ScenarioCoordinator, classify_scenario, parse_trace
from lab02_server import ServerLifecycleError


def lines_page(snap="7", end=400):
    return parse_trace([
        f"LAB_TRACE t_us=100 event=snap_start gen=0 focus=4 msg=viewed snap_id={snap} run_id=run snap_origin=touch",
        f"LAB_TRACE t_us=150 event=page_fetch gen=2 focus=16 msg=- op_id=22 snap_id={snap} revision=8 page_receipt_id=pageabc run_id=run server_completed_ns=99",
        f"LAB_TRACE t_us=200 event=page_apply gen=2 focus=4 msg=viewed op_id=22 snap_id={snap} revision=8 page_receipt_id=pageabc run_id=run server_completed_ns=99",
        f"LAB_TRACE t_us={end} event=snap_end gen=0 focus=4 msg=viewed snap_id={snap} run_id=run snap_origin=touch",
    ])


class ScenarioTraceTests(unittest.TestCase):
    def test_snap_origin_survives_parse_and_journal_relay(self):
        parsed = lines_page()
        start = next(event for event in parsed if event["event"] == "snap_start")
        end = next(event for event in parsed if event["event"] == "snap_end")
        self.assertEqual(start["snap_origin"], "touch")
        self.assertEqual(end["snap_origin"], start["snap_origin"])

        server = Mock()
        server.run_id = "run"
        rows = []
        coordinator = Lab02ScenarioCoordinator(server, capture_active=lambda: True,
                                              event_sink=rows.append)
        coordinator.ingest_line("LAB_TRACE t_us=10 event=snap_start gen=0 focus=4 msg=viewed snap_id=7 snap_origin=harness")
        coordinator.ingest_line("LAB_TRACE t_us=20 event=snap_end gen=0 focus=4 msg=viewed snap_id=7 snap_origin=harness")
        self.assertEqual([row["source"] for row in rows], ["device", "device"])
        self.assertTrue(all(row["snap_origin"] == "harness" for row in rows))
        coordinator.close()

    def test_page_receipt_requires_run_action_and_fetch_apply_pair_inside_snap(self):
        receipt = {"action_id": "act", "run_id": "run", "ok": True, "inbox_revision": 8, "receipt_before_snap_end": True, "snap_id": "7"}
        events = lines_page()
        self.assertEqual(classify_scenario("S-page-snap", events, action_receipt=receipt), "PASS")
        for bad in ({**receipt, "run_id": "other"}, {**receipt, "ok": False},
                    {**receipt, "action_id": ""}):
            self.assertEqual(classify_scenario("S-page-snap", events, action_receipt=bad), "INCOMPLETE")
        self.assertEqual(classify_scenario("S-page-snap", lines_page(end=175), action_receipt=receipt), "INCOMPLETE")
        stale = [dict(e, page_receipt_id="stale") if e["event"] == "page_apply" else e for e in events]
        self.assertEqual(classify_scenario("S-page-snap", stale, action_receipt=receipt), "INCOMPLETE")

    def test_action_effect_requires_real_paired_apply_and_semantic_identity(self):
        receipt = {"action_id": "act", "run_id": "run", "ok": True, "inbox_revision": 8,
                   "snap_id": "7", "receipt_before_snap_end": True, "removed_message_id": "viewed",
                   "viewed_message_id": "viewed", "nearest_remaining_message_id": "neighbor"}
        events = lines_page()
        effect = {"event": "scenario_effect", "action_id": "act", "run_id": "run", "snap_id": "7",
                  "revision": 8, "selected_message_id": "neighbor", "viewed_message_id": "viewed",
                  "before_message_id": "viewed", "page_receipt_id": "pageabc", "op_id": "22",
                  "apply_snap_id": "7", "apply_t_us": 200}
        self.assertEqual(classify_scenario("S-rm-focus", events + [effect], action_receipt=receipt,
                                           expected_message_id="viewed"), "PASS")
        wrong = [dict(effect, selected_message_id="other", apply_snap_id="7")]
        self.assertEqual(classify_scenario("S-rm-focus", events + wrong, action_receipt=receipt,
                                           expected_message_id="viewed"), "FAIL")
        self.assertEqual(classify_scenario("S-rm-focus", events, action_receipt=receipt,
                                           expected_message_id="viewed"), "INCOMPLETE")
        traversed = [dict(effect, apply_snap_id="8")]
        self.assertEqual(classify_scenario("S-rm-focus", events + traversed, action_receipt=receipt,
                                           expected_message_id="viewed"), "INCOMPLETE")

    def test_nominal_without_mutation_is_unmeasured(self):
        self.assertEqual(classify_scenario("nominal", [], action_receipt=None), "UNMEASURED")

    def test_late_action_receipt_after_snap_end_does_not_replay_effect(self):
        server = Mock()
        server.run_id = "run"
        started, release = threading.Event(), threading.Event()
        def post(action_id, payload, cancelled=None):
            started.set()
            release.wait(1)
            return {"ok": True, "run_id": "run", "action_id": action_id,
                    "inbox_revision": 2, "removed_message_id": "viewed",
                    "nearest_remaining_message_id": "neighbor"}
        server.post_action.side_effect = post
        events = []
        coordinator = Lab02ScenarioCoordinator(server, capture_active=lambda: True, event_sink=events.append)
        coordinator.ingest_line("LAB_TRACE t_us=10 event=snap_start gen=0 focus=4 msg=viewed snap_id=7")
        future = coordinator.dispatch_action("S-rm-focus", "remove_message", {}, viewed_message_id="viewed")
        self.assertTrue(started.wait(1))
        coordinator.ingest_line("LAB_TRACE t_us=20 event=snap_end gen=0 focus=4 msg=viewed snap_id=7")
        release.set()
        future.result(timeout=1)
        self.assertFalse(any(e.get("kind") == "scenario_effect" for e in events))
        action = next(e for e in events if e.get("kind") == "scenario_action")
        self.assertEqual(action["proof_status"], "INCOMPLETE")
        coordinator.close()

    def test_dispatch_freezes_nested_data_before_worker_runs(self):
        server = Mock()
        server.run_id = "run"
        started, release = threading.Event(), threading.Event()
        def post(action_id, payload, cancelled=None):
            started.set()
            release.wait(1)
            return {"ok": True, "run_id": "run", "action_id": action_id, "inbox_revision": 1}
        server.post_action.side_effect = post
        coordinator = Lab02ScenarioCoordinator(server, capture_active=lambda: True)
        coordinator.ingest_line("LAB_TRACE t_us=10 event=snap_start gen=0 focus=4 msg=viewed snap_id=7")
        data = {"nested": {"x": 1}}
        future = coordinator.dispatch_action("S-push", "push_item", data, viewed_message_id="viewed")
        self.assertTrue(started.wait(1))
        data["nested"]["x"] = 2
        release.set()
        future.result(timeout=1)
        self.assertEqual(server.post_action.call_args.args[1]["nested"]["x"], 1)
        coordinator.close()

    def test_effect_without_matching_fetch_or_with_other_run_is_incomplete(self):
        receipt = {"action_id": "act", "run_id": "run", "ok": True, "inbox_revision": 8,
                   "snap_id": "7", "receipt_before_snap_end": True, "removed_message_id": "viewed",
                   "nearest_remaining_message_id": "neighbor"}
        effect = {"event": "scenario_effect", "action_id": "act", "run_id": "run", "snap_id": "7",
                  "revision": 8, "selected_message_id": "neighbor", "viewed_message_id": "neighbor",
                  "page_receipt_id": "fabricated", "op_id": "99", "apply_snap_id": "7", "apply_t_us": 200}
        self.assertEqual(classify_scenario("S-rm-focus", parse_trace([
            "LAB_TRACE t_us=10 event=page_fetch gen=0 focus=4 msg=- op_id=9 snap_id=7 revision=2 page_receipt_id=pg run_id=run",
            "LAB_TRACE t_us=20 event=page_apply gen=0 focus=5 msg=neighbor op_id=9 snap_id=7 revision=2 page_receipt_id=pg run_id=run"])+[effect], action_receipt=receipt,
                                           expected_message_id="viewed"), "INCOMPLETE")
        other_run = [dict(e, run_id="other") if e["event"] == "page_apply" else e for e in lines_page()]
        self.assertEqual(classify_scenario("S-rm-focus", other_run + [effect], action_receipt=receipt,
                                           expected_message_id="viewed"), "INCOMPLETE")

    def test_valid_apply_after_receipt_before_snap_end_is_recorded(self):
        server = Mock()
        server.run_id = "run"
        server.post_action.return_value = {"ok": True, "run_id": "run", "action_id": "pending",
                                          "inbox_revision": 2, "removed_message_id": "viewed",
                                          "nearest_remaining_message_id": "neighbor"}
        events = []
        coordinator = Lab02ScenarioCoordinator(server, capture_active=lambda: True, event_sink=events.append)
        coordinator.ingest_line("LAB_TRACE t_us=10 event=snap_start gen=0 focus=4 msg=viewed snap_id=7")
        # Preserve action identity from the generated request in its matching server receipt.
        server.post_action.side_effect = lambda action_id, payload, cancelled=None: {
            "ok": True, "run_id": "run", "action_id": action_id, "inbox_revision": 2,
            "removed_message_id": "viewed", "nearest_remaining_message_id": "neighbor"}
        _, receipt = coordinator.action("S-rm-focus", "remove_message", {}, viewed_message_id="viewed")
        coordinator.ingest_line("LAB_TRACE t_us=15 event=snap_end gen=0 focus=4 msg=viewed snap_id=7")
        coordinator.ingest_line("LAB_TRACE t_us=16 event=page_fetch gen=0 focus=4 msg=- op_id=9 snap_id=7 revision=2 page_receipt_id=pg run_id=run")
        coordinator.ingest_line("LAB_TRACE t_us=6000000 event=page_apply gen=0 focus=5 msg=neighbor op_id=9 snap_id=7 revision=2 page_receipt_id=pg run_id=run")
        self.assertTrue(any(e.get("kind") == "scenario_effect" and e.get("selected_message_id") == "neighbor" and e.get("snap_id") == "7" and e.get("apply_snap_id") == "7" for e in events))
        effect = next(e for e in events if e.get("kind") == "scenario_effect")
        evidence = [{k: v for k, v in e.items() if k != "kind"} for e in events if e.get("kind") == "device_trace"] + [effect]
        self.assertEqual(classify_scenario("S-rm-focus", evidence, action_receipt=receipt,
                                           expected_message_id="viewed"), "PASS")
        coordinator.close()

    def test_ingest_rejects_other_run_and_unmatched_apply(self):
        server = Mock()
        server.run_id = "run"
        server.post_action.side_effect = lambda action_id, payload, cancelled=None: {
            "ok": True, "run_id": "run", "action_id": action_id, "inbox_revision": 2,
            "removed_message_id": "viewed", "nearest_remaining_message_id": "neighbor"}
        events = []
        coordinator = Lab02ScenarioCoordinator(server, capture_active=lambda: True, event_sink=events.append)
        coordinator.ingest_line("LAB_TRACE t_us=10 event=snap_start gen=0 focus=4 msg=viewed snap_id=7")
        coordinator.action("S-rm-focus", "remove_message", {}, viewed_message_id="viewed")
        coordinator.ingest_line("LAB_TRACE t_us=15 event=snap_end gen=0 focus=4 msg=viewed snap_id=7")
        coordinator.ingest_line("LAB_TRACE t_us=16 event=page_apply gen=0 focus=5 msg=neighbor op_id=9 snap_id=7 revision=2 page_receipt_id=pg run_id=run")
        coordinator.ingest_line("LAB_TRACE t_us=17 event=page_fetch gen=0 focus=4 msg=- op_id=9 snap_id=7 revision=2 page_receipt_id=pg run_id=other")
        coordinator.ingest_line("LAB_TRACE t_us=18 event=page_apply gen=0 focus=5 msg=neighbor op_id=9 snap_id=7 revision=2 page_receipt_id=pg run_id=other")
        self.assertFalse(any(e.get("kind") == "scenario_effect" for e in events))
        coordinator.close()

    def test_action_requires_capture_and_active_snap(self):
        server = Mock()
        server.run_id = "run"
        coordinator = Lab02ScenarioCoordinator(server, capture_active=lambda: False)
        with self.assertRaisesRegex(ServerLifecycleError, "capture_not_active"):
            coordinator.action("S-push", "push_item", {})
        self.assertFalse(server.post_action.called)
        with self.assertRaisesRegex(ServerLifecycleError, "action_requires_active_snap"):
            Lab02ScenarioCoordinator(server, capture_active=lambda: True).action("S-rm-focus", "remove_message", {}, viewed_message_id="x")

    def test_close_cancels_and_joins_dispatch_worker(self):
        server = Mock()
        server.run_id = "run"
        started, release = threading.Event(), threading.Event()
        def post(action_id, payload, cancelled=None):
            started.set()
            release.wait(1)
            return {"ok": True, "run_id": "run", "action_id": action_id, "inbox_revision": 2}
        server.post_action.side_effect = post
        events = []
        coordinator = Lab02ScenarioCoordinator(server, capture_active=lambda: True, event_sink=events.append)
        coordinator.ingest_line("LAB_TRACE t_us=10 event=snap_start gen=0 focus=4 msg=viewed snap_id=7")
        future = coordinator.dispatch_action("S-push", "push_item", {}, viewed_message_id="viewed")
        self.assertTrue(started.wait(1))
        with self.assertRaisesRegex(ServerLifecycleError, "scenario_action_join_timeout"):
            coordinator.close(timeout=0.01)
        release.set()
        with self.assertRaises(ServerLifecycleError):
            future.result(timeout=1)
        self.assertFalse(any(e.get("kind") == "scenario_action" for e in events))
        coordinator.close(timeout=1)


if __name__ == "__main__":
    unittest.main()
