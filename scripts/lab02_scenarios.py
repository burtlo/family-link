"""LAB-02 managed scenario actions and evidence classification."""
from __future__ import annotations

import re
import copy
import threading
import uuid
from concurrent.futures import Future
from dataclasses import dataclass
from typing import Callable, Iterable

from lab02_server import ManagedLab02Server, ServerLifecycleError

PROOF_SCENARIOS = {"S-page-snap", "S-push", "S-rm-focus"}
TRACE = re.compile(r"LAB_TRACE t_us=(\d+) event=(\S+) gen=(\d+) focus=(-?\d+) msg=(\S+)(?:\s+(.*))?")


@dataclass
class ScenarioResult:
    workflow_status: str
    experiment_verdict: str
    transport_status: str
    reason: str
    action_id: str
    receipt: dict | None = None


def parse_trace(lines: Iterable[str]) -> list[dict]:
    events = []
    for line in lines:
        match = TRACE.search(line)
        if not match:
            continue
        t_us, name, generation, focus, message_id, extra = match.groups()
        fields = {}
        for part in (extra or "").split():
            if "=" in part:
                key, value = part.split("=", 1)
                fields[key] = value
        events.append({"t_us": int(t_us), "event": name, "generation": int(generation),
                       "focus": int(focus), "message_id": None if message_id == "-" else message_id,
                       **fields})
    return events


def _valid_receipt(receipt: dict | None) -> bool:
    return bool(receipt and receipt.get("ok") is True and receipt.get("run_id")
                and receipt.get("action_id") and receipt.get("inbox_revision") is not None)


def _paired_page(events: list[dict], receipt: dict) -> bool:
    if not _valid_receipt(receipt):
        return False
    run_id = receipt["run_id"]
    if not receipt.get("receipt_before_snap_end") or not receipt.get("snap_id"):
        return False
    for start in events:
        if (start["event"] != "snap_start" or start.get("run_id") != run_id
                or start.get("snap_id") != receipt.get("snap_id")):
            continue
        snap_id = start.get("snap_id")
        end = next((e for e in events if e["event"] == "snap_end"
                    and e.get("snap_id") == snap_id and e.get("run_id") == run_id
                    and e["t_us"] >= start["t_us"]), None)
        if not end:
            continue
        for page in events:
            if (page["event"] != "page_fetch" or page.get("run_id") != run_id
                    or page.get("snap_id") != snap_id or not page.get("page_receipt_id")
                    or page.get("op_id") is None or int(page.get("revision", -1)) < int(receipt["inbox_revision"])):
                continue
            applied = next((e for e in events if e["event"] == "page_apply"
                            and e.get("run_id") == run_id and e.get("snap_id") == snap_id
                            and e.get("op_id") == page.get("op_id")
                            and e.get("page_receipt_id") == page.get("page_receipt_id")
                            and e.get("run_id") == page.get("run_id")
                            and int(e.get("revision", -1)) >= int(page.get("revision", 1 << 62))
                            and page["t_us"] <= e["t_us"] <= end["t_us"]), None)
            if start["t_us"] <= page["t_us"] <= end["t_us"] and applied:
                return True
    return False


def classify_scenario(name: str, events: list[dict], *, action_receipt: dict | None,
                      expected_message_id: str | None = None) -> str:
    """Return experiment verdict only; host transport is reported separately."""
    if name == "nominal":
        return "UNMEASURED"
    if not _valid_receipt(action_receipt):
        return "INCOMPLETE"
    if name == "S-page-snap":
        return "PASS" if _paired_page(events, action_receipt) else "INCOMPLETE"
    if name in {"S-push", "S-rm-focus"}:
        effect = next((e for e in events if e["event"] == "scenario_effect"
                       and e.get("action_id") == action_receipt.get("action_id")
                       and e.get("run_id") == action_receipt.get("run_id")
                       and e.get("snap_id") == action_receipt.get("snap_id")
                       and action_receipt.get("receipt_before_snap_end") is True), None)
        if (not effect or int(effect.get("revision", -1)) < int(action_receipt["inbox_revision"])
                or not effect.get("page_receipt_id") or effect.get("op_id") is None):
            return "INCOMPLETE"
        fetch = next((e for e in events if e.get("event") == "page_fetch"
                      and e.get("run_id") == action_receipt.get("run_id")
                      and e.get("snap_id") == action_receipt.get("snap_id")
                      and e.get("op_id") == effect.get("op_id")
                      and e.get("page_receipt_id") == effect.get("page_receipt_id")
                      and int(e.get("revision", -1)) >= int(action_receipt["inbox_revision"])
                      and int(e.get("t_us", -1)) <= int(effect.get("apply_t_us", -2))), None)
        apply = next((e for e in events if e.get("event") == "page_apply"
                      and e.get("run_id") == action_receipt.get("run_id")
                      and e.get("snap_id") == action_receipt.get("snap_id")
                      and e.get("op_id") == effect.get("op_id")
                      and e.get("page_receipt_id") == effect.get("page_receipt_id")
                      and int(e.get("revision", -1)) >= int(action_receipt["inbox_revision"])
                      and e.get("message_id") == effect.get("viewed_message_id")), None)
        if not fetch or not apply or effect.get("apply_t_us") != apply.get("t_us"):
            return "INCOMPLETE"
        if str(effect.get("apply_snap_id")) != str(action_receipt.get("snap_id")):
            return "INCOMPLETE"
        if effect.get("before_message_id") != action_receipt.get("viewed_message_id"):
            return "INCOMPLETE"
        if name == "S-push":
            return "PASS" if effect.get("viewed_message_id") == expected_message_id else "FAIL"
        if action_receipt.get("removed_message_id") != expected_message_id:
            return "INCOMPLETE"
        expected = action_receipt.get("nearest_remaining_message_id")
        return "PASS" if expected and effect.get("selected_message_id") == expected else "FAIL" if expected else "INCOMPLETE"
    return "UNMEASURED"


class Lab02ScenarioCoordinator:
    """Serializes actions and accepts only same-snap, post-receipt apply evidence."""
    def __init__(self, server: ManagedLab02Server, *, capture_active: Callable[[], bool],
                 event_sink: Callable[[dict], None] | None = None):
        self.server = server
        self.capture_active = capture_active
        self.event_sink = event_sink or (lambda event: None)
        self._state_lock = threading.RLock()
        self._action_lock = threading.Lock()
        self._active_snap: dict | None = None
        self._pending_action: dict | None = None
        self._action_thread: threading.Thread | None = None
        self._dispatch_lock = threading.Lock()
        self._closed = False
        self._cancel = threading.Event()
        self._page_fetches: list[dict] = []

    def ingest_line(self, line: str) -> None:
        for event in parse_trace((line,)):
            with self._state_lock:
                if event["event"] == "snap_start":
                    event.setdefault("run_id", self.server.run_id)
                    if event.get("run_id") != self.server.run_id:
                        self._active_snap = None
                        continue
                    self._active_snap = dict(event)
                else:
                    # The sole serial reader supplies run ownership; device snap IDs remain authoritative.
                    event.setdefault("run_id", self.server.run_id)
                self.event_sink({"kind": "device_trace", **event})
                if event["event"] == "page_fetch":
                    if event.get("run_id") == self.server.run_id and event.get("page_receipt_id") not in (None, "", "-") and event.get("op_id") is not None:
                        self._page_fetches.append(dict(event))
                        self._page_fetches = self._page_fetches[-64:]
                pending = self._pending_action
                if event["event"] == "page_apply" and pending and pending.get("receipt"):
                    receipt = pending["receipt"]
                    if (pending.get("receipt_before_snap_end") is True
                            and receipt.get("ok") is True
                            and receipt.get("run_id") == pending.get("run_id")
                            and receipt.get("action_id") == pending.get("action_id")
                            and event.get("page_receipt_id") not in (None, "", "-")
                            and event.get("op_id") is not None
                            and event.get("run_id") == pending.get("run_id")
                            and event.get("snap_id") == pending.get("snap_id")
                            and any(f.get("run_id") == pending.get("run_id")
                                    and f.get("snap_id") == pending.get("snap_id")
                                    and f.get("op_id") == event.get("op_id")
                                    and f.get("page_receipt_id") == event.get("page_receipt_id")
                                    and int(f.get("revision", -1)) >= int(receipt.get("inbox_revision", 1 << 62))
                                    and f.get("t_us", -1) <= event.get("t_us", -1)
                                    for f in self._page_fetches)
                            and int(event.get("revision", -1)) >= int(receipt.get("inbox_revision", 1 << 62))):
                        effect = {"kind": "scenario_effect", "event": "scenario_effect",
                                  "run_id": pending["run_id"], "action_id": pending["action_id"],
                                  "scenario": pending["scenario"], "snap_id": pending["snap_id"],
                                  "apply_snap_id": event.get("snap_id"),
                                  "before_message_id": pending["viewed_message_id"],
                                  "viewed_message_id": event.get("message_id"),
                                  "removed_message_id": receipt.get("removed_message_id"),
                                  "selected_message_id": event.get("message_id"),
                                  "revision": event.get("revision"), "page_receipt_id": event.get("page_receipt_id"),
                                  "op_id": event.get("op_id"), "apply_t_us": event["t_us"]}
                        self.event_sink(effect)
                        pending["apply_evidence"] = effect
                if event["event"] == "snap_end" and self._active_snap and event.get("snap_id") == self._active_snap.get("snap_id"):
                    if self._pending_action and self._pending_action.get("snap_id") == event.get("snap_id"):
                        self._pending_action["receipt_before_snap_end"] = bool(self._pending_action.get("receipt"))
                        self._pending_action["snap_ended"] = True
                    self._active_snap = None
                self._refresh_pending_verdict()

    def _refresh_pending_verdict(self) -> None:
        pending = self._pending_action
        if pending and pending.get("receipt") and pending.get("snap_ended"):
            return

    def action(self, scenario: str, name: str, data: dict, *, during_snap: bool = False,
               viewed_message_id: str | None = None, _submission: dict | None = None) -> tuple[str, dict]:
        data = copy.deepcopy(data)
        if not self.capture_active():
            raise ServerLifecycleError("capture_not_active")
        if scenario in PROOF_SCENARIOS and not self._active_snap and _submission is None:
            raise ServerLifecycleError("action_requires_active_snap")
        with self._action_lock:
            with self._state_lock:
                if self._closed:
                    raise ServerLifecycleError("coordinator_closed")
                snap = dict((_submission or {}).get("snap") or self._active_snap or {})
                run_id = (_submission or {}).get("run_id", self.server.run_id)
                if scenario in PROOF_SCENARIOS:
                    if not snap or (not self._active_snap and _submission is None):
                        raise ServerLifecycleError("action_requires_active_snap")
                    if scenario in {"S-push", "S-rm-focus"} and not viewed_message_id:
                        raise ServerLifecycleError("viewed_identity_required")
                    if (scenario in {"S-push", "S-rm-focus"}
                            and snap.get("message_id") is not None
                            and snap.get("message_id") != viewed_message_id):
                        raise ServerLifecycleError("viewed_identity_mismatch")
                    if scenario in {"S-push", "S-rm-focus"}:
                        self._pending_action = {"action_id": (_submission or {}).get("action_id", uuid.uuid4().hex), "scenario": scenario,
                                                "run_id": run_id, "snap_id": snap.get("snap_id"),
                                                "viewed_message_id": viewed_message_id,
                                                "focus": snap.get("focus"), "receipt": None,
                                                "receipt_before_snap_end": False}
                action_id = ((self._pending_action["action_id"] if scenario in {"S-push", "S-rm-focus"}
                              else (_submission or {}).get("action_id")) or uuid.uuid4().hex)
            payload = {"name": name, **data}
            if scenario == "S-rm-focus":
                payload["viewed_message_id"] = viewed_message_id
            receipt = self.server.post_action(action_id, payload, cancelled=self._cancel)
            with self._state_lock:
                if self._closed:
                    raise ServerLifecycleError("coordinator_closed")
                if scenario in PROOF_SCENARIOS:
                    pending = self._pending_action if scenario in {"S-push", "S-rm-focus"} else None
                    if (self._active_snap is None
                            or (self._active_snap is not None and self._active_snap.get("snap_id") != snap.get("snap_id"))
                            or run_id != self.server.run_id
                            or not _valid_receipt(receipt) or receipt.get("run_id") != run_id
                            or receipt.get("action_id") != action_id):
                        self.event_sink({"kind": "scenario_action", "run_id": run_id,
                                         "action_id": action_id, "scenario": scenario, "receipt": receipt,
                                         "proof_status": "INCOMPLETE", "reason": "receipt_after_snap_end"})
                    else:
                        receipt = {**receipt, "snap_id": snap.get("snap_id"),
                                   "viewed_message_id": viewed_message_id,
                                   "receipt_before_snap_end": True}
                        if pending and pending.get("action_id") == action_id:
                            pending["receipt"] = receipt
                            pending["receipt_before_snap_end"] = True
                self.event_sink({"kind": "scenario_action", "run_id": run_id,
                                 "action_id": action_id, "scenario": scenario,
                                 "snap_id": snap.get("snap_id"), "viewed_message_id": viewed_message_id,
                                 "receipt": receipt})
            return action_id, receipt

    def dispatch_action(self, scenario: str, name: str, data: dict, *,
                        viewed_message_id: str | None = None) -> Future:
        future: Future = Future()
        frozen = copy.deepcopy(data)
        with self._dispatch_lock:
            if self._closed:
                raise ServerLifecycleError("coordinator_closed")
            if self._action_thread is not None and self._action_thread.is_alive():
                raise ServerLifecycleError("action_in_progress")
            with self._state_lock:
                snap = copy.deepcopy(self._active_snap or {})
                if scenario in PROOF_SCENARIOS and not snap:
                    raise ServerLifecycleError("action_requires_active_snap")
                submission = {"snap": snap, "run_id": self.server.run_id,
                              "viewed_message_id": viewed_message_id,
                              "focus": snap.get("focus"), "action_id": uuid.uuid4().hex,
                              "scenario": scenario, "name": name}
            def run_action() -> None:
                try:
                    future.set_result(self.action(scenario, name, frozen,
                                                  viewed_message_id=viewed_message_id, _submission=submission))
                except BaseException as exc:
                    future.set_exception(exc)
            self._action_thread = threading.Thread(target=run_action, name="lab02-scenario-action", daemon=True)
            self._action_thread.start()
        return future

    def close(self, timeout: float = 1.0) -> None:
        with self._dispatch_lock:
            self._closed = True
            self._cancel.set()
            worker = self._action_thread
        if worker:
            worker.join(timeout=max(0.0, timeout))
            if worker.is_alive():
                raise ServerLifecycleError("scenario_action_join_timeout")
        with self._state_lock:
            self._pending_action = None
            self._active_snap = None

    def run(self, scenario: str, name: str, data: dict, *, during_snap: bool = False,
            viewed_message_id: str | None = None, trace_lines: Iterable[str] = ()) -> ScenarioResult:
        action_id = ""
        try:
            action_id, receipt = self.action(scenario, name, data, during_snap=during_snap,
                                             viewed_message_id=viewed_message_id)
        except ServerLifecycleError as exc:
            return ScenarioResult("FAIL", "INCOMPLETE", "FAIL", str(exc), action_id)
        events = parse_trace(trace_lines)
        verdict = classify_scenario(scenario, events, action_receipt=receipt,
                                    expected_message_id=viewed_message_id)
        return ScenarioResult("PASS", verdict, "PASS", "", action_id, receipt)
