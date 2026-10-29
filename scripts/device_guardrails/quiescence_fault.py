"""Host quiescence checks (LIFE-QUIESCE).

The pure model is the fault injection: destroying storage while work is in
flight fails; acknowledging a terminal state first passes. The source scans
record whether lab 16 waits for its stack job, and whether product HTTP
worker stop still deletes the task and the queue in one function.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEVICE_MAIN = REPO / "client/device/main"
LAB16_APP = REPO / "client/labs/lab-16-attached-storage/main/app_main.c"

PRODUCT_STOPS = (
    "product_login_http_worker.cpp",
    "product_inbox_http_worker.cpp",
    "product_send_http_worker.cpp",
)


@dataclass
class Lab16HarnessState:
    stream_active: bool = False
    upload_started: bool = False
    worker_done: bool = False
    destroyed: bool = False
    errors: list[str] = field(default_factory=list)


def lab16_attempt_destroy(state: Lab16HarnessState) -> list[str]:
    """Stack job storage may be released only after the upload worker is done."""
    if state.destroyed and (state.stream_active or state.upload_started) and not state.worker_done:
        return ["lab16: job storage destroyed before upload worker acknowledged done"]
    return []


@dataclass
class ProductWorkerState:
    inbox_jobs_in_flight: int = 0
    send_jobs_in_flight: int = 0
    worker_acked: bool = False
    queue_destroyed: bool = False


def product_teardown_while_jobs_active(state: ProductWorkerState) -> list[str]:
    """Queue storage may be destroyed only after in-flight jobs have acked."""
    if state.queue_destroyed and not state.worker_acked and (
        state.inbox_jobs_in_flight or state.send_jobs_in_flight
    ):
        return ["product: queue destroyed with in-flight HTTP jobs and no worker ack"]
    return []


def inject_lab16_fault() -> list[str]:
    bad = Lab16HarnessState(stream_active=True, upload_started=True, worker_done=False, destroyed=True)
    return lab16_attempt_destroy(bad)


def inject_lab16_quiesced() -> list[str]:
    good = Lab16HarnessState(stream_active=False, upload_started=True, worker_done=True, destroyed=True)
    return lab16_attempt_destroy(good)


def inject_product_fault() -> list[str]:
    bad = ProductWorkerState(inbox_jobs_in_flight=1, send_jobs_in_flight=1, queue_destroyed=True)
    return product_teardown_while_jobs_active(bad)


def inject_product_quiesced() -> list[str]:
    good = ProductWorkerState(
        inbox_jobs_in_flight=0,
        send_jobs_in_flight=0,
        worker_acked=True,
        queue_destroyed=True,
    )
    return product_teardown_while_jobs_active(good)


def _strip(text: str) -> str:
    without_block = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", without_block)


def _functions(text: str) -> list[tuple[str, str]]:
    text = _strip(text).replace('extern "C"', " ")
    found: list[tuple[str, str]] = []
    pattern = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*\{")
    for match in pattern.finditer(text):
        name = match.group(1)
        if name in {"if", "for", "while", "switch"}:
            continue
        start = match.end() - 1
        depth = 0
        body = None
        for index in range(start, len(text)):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    body = text[start : index + 1]
                    break
        if body is not None:
            found.append((name, body))
    return found


def product_stop_without_ack(root: Path | None = None) -> list[str]:
    """Product HTTP stop functions that delete the worker and its queue together."""
    root = root or DEVICE_MAIN
    errors: list[str] = []
    for name in PRODUCT_STOPS:
        path = root / name
        if not path.is_file():
            errors.append(f"missing worker: {name}")
            continue
        text = path.read_text(encoding="utf-8")
        bodies = [body for fn, body in _functions(text) if fn.endswith("_http_worker_stop") or fn == "worker_stop"]
        if not bodies:
            errors.append(f"{name}: worker stop function not found")
            continue
        for body in bodies:
            deletes_task = "vTaskDelete" in body
            deletes_queue = "vQueueDelete" in body
            waits = "xEventGroupWaitBits" in body or "worker_acked" in body
            if deletes_task and deletes_queue and not waits:
                errors.append(
                    f"{name}: vTaskDelete and vQueueDelete without a worker acknowledgement"
                )
    return errors


def lab16_stack_job_errors(text: str) -> list[str]:
    """A stack-allocated upload job must wait for DONE before the function returns."""
    errors: list[str] = []
    for name, body in _functions(text):
        if "lab16_upload_job_t job" not in body or "lab16_upload_job_start" not in body:
            continue
        if "lab16_upload_job_wait" not in body or "LAB16_UPLOAD_EVT_DONE" not in body:
            errors.append(
                f"{name}: stack lab16_upload_job_t started without waiting for LAB16_UPLOAD_EVT_DONE"
            )
    return errors


def lab16_live_stack_job_errors() -> list[str]:
    if not LAB16_APP.is_file():
        return [f"missing {LAB16_APP}"]
    return lab16_stack_job_errors(LAB16_APP.read_text(encoding="utf-8"))
