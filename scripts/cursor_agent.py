#!/usr/bin/env python3
"""Run a scoped Cursor Cloud Agent against this repository.

The script intentionally uses only Python's standard library.  It creates a
Cursor Cloud Agent on the repository's configured GitHub remote, pins the
Composer 2.5 non-fast variant, streams progress, and records the conversation
under .cursor/agent-runs/ for later inspection.

Examples:
    python3 scripts/cursor_agent.py "Improve the repository README"
    python3 scripts/cursor_agent.py --agent-id bc-... "Continue the work"
    python3 scripts/cursor_agent.py --status --agent-id bc-...
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


API_ROOT = "https://api.cursor.com/v1"
REPO_URL = "https://github.com/burtlo/family-link.git"
REPO_SLUG = "github.com/burtlo/family-link"
MODEL_ID = "composer-2.5"
TERMINAL = {"FINISHED", "ERROR", "CANCELLED", "EXPIRED"}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def require_repo_scope() -> tuple[Path, str]:
    root = repo_root()
    actual_root = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "--show-toplevel"], text=True
    ).strip()
    if Path(actual_root).resolve() != root.resolve():
        raise RuntimeError(f"unexpected repository root: {actual_root}")
    remote = subprocess.check_output(
        ["git", "-C", str(root), "remote", "get-url", "origin"], text=True
    ).strip()
    normalized = remote.removesuffix(".git").rstrip("/")
    if normalized != REPO_URL.removesuffix(".git"):
        raise RuntimeError(
            f"origin must be {REPO_URL}; refusing to operate on {remote}"
        )
    return root, remote


def api_key() -> str:
    value = os.environ.get("FOUNDRY_CURSOR_API_KEY", "").strip()
    if not value:
        raise RuntimeError("FOUNDRY_CURSOR_API_KEY is not set")
    return value


def request(
    method: str,
    path: str,
    *,
    body: dict[str, Any] | None = None,
    accept: str = "application/json",
) -> tuple[dict[str, Any], dict[str, str]]:
    payload = None if body is None else json.dumps(body).encode()
    headers = {
        "Authorization": f"Bearer {api_key()}",
        "Accept": accept,
        "User-Agent": "family-link-cursor-agent/1.0",
    }
    if payload is not None:
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(
        f"{API_ROOT}{path}", data=payload, headers=headers, method=method
    )
    try:
        with urllib.request.urlopen(req, timeout=60) as response:
            raw = response.read()
            return (json.loads(raw) if raw else {}, dict(response.headers.items()))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"Cursor API {exc.code} for {method} {path}: {detail}") from exc


def scoped_prompt(prompt: str, root: Path) -> str:
    return f"""You are working only in the Family Link repository.

Repository: {REPO_SLUG}
Local checkout reference: {root}

Scope rules:
- Work only on this repository and its files.
- Do not modify unrelated repositories, external services, or deployment systems.
- Inspect the existing repository before changing anything.
- Keep changes focused on the requested task.
- When you make changes, commit them and push the commit to the generated
  `cursor/...` branch. Never push directly to the starting branch.
- Run appropriate lightweight checks when practical and report what you ran.
- Do not commit secrets, credentials, generated build output, or unrelated local artifacts.

User task:
{prompt.strip()}
"""


def log_path(run_id: str) -> Path:
    directory = repo_root() / ".cursor" / "agent-runs"
    directory.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return directory / f"{stamp}-{run_id}.jsonl"


def write_log(path: Path, event: str, data: Any) -> None:
    with path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps({"event": event, "data": data}, sort_keys=True) + "\n")


def create_agent(
    prompt: str,
    *,
    starting_ref: str,
    auto_create_pr: bool,
) -> dict[str, Any]:
    body = {
        "prompt": {"text": prompt},
        "model": {"id": MODEL_ID, "params": [{"id": "fast", "value": "false"}]},
        "repos": [{"url": REPO_URL, "startingRef": starting_ref}],
        # Always send the field explicitly so a server-side default cannot
        # accidentally change the branch safety policy.
        "workOnCurrentBranch": False,
        "autoCreatePR": auto_create_pr,
    }
    result, _ = request("POST", "/agents", body=body)
    return result


def create_run(agent_id: str, prompt: str) -> dict[str, Any]:
    body = {"prompt": {"text": prompt}, "mode": "agent"}
    result, _ = request(
        "POST", f"/agents/{urllib.parse.quote(agent_id)}/runs", body=body
    )
    return result


def get_run(agent_id: str, run_id: str) -> dict[str, Any]:
    result, _ = request(
        "GET",
        f"/agents/{urllib.parse.quote(agent_id)}/runs/{urllib.parse.quote(run_id)}",
    )
    return result


def stream_run(agent_id: str, run_id: str, log: Path) -> dict[str, Any] | None:
    path = f"/agents/{urllib.parse.quote(agent_id)}/runs/{urllib.parse.quote(run_id)}/stream"
    req = urllib.request.Request(
        f"{API_ROOT}{path}",
        headers={
            "Authorization": f"Bearer {api_key()}",
            "Accept": "text/event-stream",
            "User-Agent": "family-link-cursor-agent/1.0",
        },
    )
    try:
        response = urllib.request.urlopen(req, timeout=60)
    except urllib.error.HTTPError as exc:
        if exc.code == 410:
            return None
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"Cursor stream failed ({exc.code}): {detail}") from exc

    event_name = "message"
    data_lines: list[str] = []
    final: dict[str, Any] | None = None
    with response:
        for raw_line in response:
            line = raw_line.decode("utf-8", errors="replace").rstrip("\r\n")
            if line.startswith("event:"):
                event_name = line.partition(":")[2].strip() or "message"
            elif line.startswith("data:"):
                data_lines.append(line.partition(":")[2].lstrip())
            elif line == "" and data_lines:
                raw_data = "\n".join(data_lines)
                data_lines = []
                try:
                    data: Any = json.loads(raw_data)
                except json.JSONDecodeError:
                    data = {"raw": raw_data}
                write_log(log, event_name, data)
                if event_name in {"assistant", "result"} and isinstance(data, dict):
                    text = data.get("text")
                    if text:
                        print(f"[{event_name}] {text}", flush=True)
                    if event_name == "result":
                        final = data
                elif event_name == "status":
                    print(f"[status] {data}", flush=True)
                elif event_name == "tool_call" and isinstance(data, dict):
                    print(
                        f"[tool] {data.get('name', 'unknown')} {data.get('status', '')}".strip(),
                        flush=True,
                    )
                if event_name == "done":
                    break
    return final


def wait_for_run(agent_id: str, run_id: str, log: Path, interval: float) -> dict[str, Any]:
    streamed = stream_run(agent_id, run_id, log)
    if streamed is not None:
        result = get_run(agent_id, run_id)
        write_log(log, "final", result)
        return result

    while True:
        result = get_run(agent_id, run_id)
        write_log(log, "poll", result)
        run = result.get("run", result)
        status = str(run.get("status", "")).upper()
        print(f"[status] {status}", flush=True)
        if status in TERMINAL:
            return result
        time.sleep(interval)


def require_pushed_cursor_branch(result: dict[str, Any]) -> None:
    run = result.get("run", result)
    branches = ((run.get("git") or {}).get("branches") or [])
    matching = [
        branch
        for branch in branches
        if branch.get("repoUrl", "").removesuffix(".git").rstrip("/") == REPO_SLUG
        and str(branch.get("branch", "")).startswith("cursor/")
    ]
    if not matching:
        raise RuntimeError(
            "Cursor finished without reporting a pushed cursor/... branch for family-link"
        )
    for branch in matching:
        print(f"pushed_branch={branch['branch']}")
        if branch.get("prUrl"):
            print(f"pull_request={branch['prUrl']}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prompt", nargs="?", help="task to send to Cursor")
    parser.add_argument("--agent-id", help="continue an existing Cursor agent")
    parser.add_argument("--status", action="store_true", help="show an agent's latest run")
    parser.add_argument("--starting-ref", default="main")
    parser.add_argument("--auto-create-pr", action="store_true")
    parser.add_argument("--no-wait", action="store_true", help="return after enqueueing")
    parser.add_argument("--poll-interval", type=float, default=8.0)
    args = parser.parse_args()

    root, _ = require_repo_scope()
    if args.status:
        if not args.agent_id:
            parser.error("--status requires --agent-id")
        agent_result, _ = request(
            "GET", f"/agents/{urllib.parse.quote(args.agent_id)}"
        )
        latest_run_id = agent_result.get("latestRunId")
        if latest_run_id:
            run_result = get_run(args.agent_id, str(latest_run_id))
            print(json.dumps({"agent": agent_result, "run": run_result}, indent=2))
        else:
            print(json.dumps({"agent": agent_result}, indent=2))
        return 0
    if not args.prompt:
        parser.error("a prompt is required unless --status is used")

    prompt = scoped_prompt(args.prompt, root)
    if args.agent_id:
        result = create_run(args.agent_id, prompt)
        agent_id = args.agent_id
    else:
        result = create_agent(
            prompt,
            starting_ref=args.starting_ref,
            auto_create_pr=args.auto_create_pr,
        )
        agent_id = result.get("agent", {}).get("id")
    run = result.get("run", {})
    run_id = run.get("id")
    if not agent_id or not run_id:
        print(json.dumps(result, indent=2))
        raise RuntimeError("Cursor create response did not include agent and run IDs")

    log = log_path(str(run_id))
    write_log(log, "created", result)
    print(f"agent_id={agent_id}")
    print(f"run_id={run_id}")
    print(f"log={log}")
    if args.no_wait:
        print(json.dumps(result, indent=2))
        return 0

    final = wait_for_run(str(agent_id), str(run_id), log, args.poll_interval)
    print(json.dumps(final, indent=2))
    if str(final.get("run", final).get("status", "")).upper() != "FINISHED":
        return 1
    require_pushed_cursor_branch(final)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(2)
