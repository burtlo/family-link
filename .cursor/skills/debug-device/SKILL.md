---
name: debug-device
description: >-
  Interactive hardware debugging session with the user for product firmware on
  the ESP32-S3 kit: form ranked hypotheses, instrument with tagged serial logs,
  start a headless serial log, ask the user to reproduce, read the log, apply
  one evidence-backed fix, rebuild and flash, and have the user reproduce again
  until the device proves it. Use with /debug-device, or when the user reports
  a device bug, a crash, a connection failure, or says a previous fix did not
  work or caused a regression.
---

# Debug device (interactive)

This is a loop you run **in the main chat with the user**, because it pauses
for them to press buttons on the kit. Do not hand it to a subagent: subagents
cannot stop and wait for a reproduction.

The loop is:

```text
intake → baseline → hypotheses → instrument → build
  → [stop log] flash → [start log] → user reproduces → read log → decide
  → fix one cause → host tests → [stop log] build + flash → [start log]
  → user reproduces + regression checks → read log → close or loop
```

The rules in [device-firmware-guardrails](../device-firmware-guardrails/SKILL.md)
apply to every change you make here, including debug logging. Read that skill
before Phase 3 if you have not already.

## Ground rules for the session

1. **Evidence before edits.** No product-code fix until a log line, a
   measurement, or a failing host test supports the hypothesis it addresses.
2. **The user is the ground truth.** If they say the bug is still there,
   it is. Do not explain why it should work. Collect new evidence.
3. **One cause per iteration.** Do not stack speculative fixes. A fix that did
   not change device behavior is reverted before the next one.
4. **Never claim "fixed" yourself.** The session closes only when the user
   confirms the reproduction no longer fails **and** the log shows the expected
   path. Report the claim class you reached.
5. **Keep the ledger** (Phase 8 format) in every update so the user can see
   what was tried, on which build, and what the log said.
6. **Ask, don't guess, when blocked.** Use the AskQuestion tool for
   reproduction status and decisions. Keep each question short.

## Serial log mechanics (read once)

- `make app:monitor` (`idf.py monitor`) is **interactive** and needs a TTY.
  Do not run it from the agent shell. Use the headless logger instead:

  ```powershell
  .venv\Scripts\python.exe scripts\app_host.py log --kit <kit> --out logs/device/debug/<session>-<n>.log
  ```

  (`make app:log KIT=<kit> OUT=<path>` is the same thing when `make` is on PATH;
  macOS uses `.venv/bin/python`.) It needs no TTY, writes host-timestamped lines
  as they arrive, and on a chip reset writes `[host] serial lost` /
  `[host] serial reconnected` and keeps going. Add `--seconds N` for a bounded
  capture.
- Start it with the Shell tool **in the background** (`block_until_ms: 0`), then
  read the `--out` file with Read/Grep. Always pass an explicit `--out` so you
  know the path. Use a new file per iteration.
- **One process can own the serial port on Windows.** Stop the logger before
  every flash, and ask the user to close any monitor of their own:

  ```powershell
  Get-CimInstance Win32_Process -Filter "Name like 'python%'" | Where-Object { $_.CommandLine -like '*app_host.py*log*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
  ```

  macOS: `pkill -f "app_host.py log"`.
- If the log shows `[host] serial lost: ... Access is denied` (or
  `PermissionError`) and never reconnects, another program holds the port.
  Ask the user to close it.
- `app_host.py flash` already waits for `product_ui_ready build=<id>` and saves
  a boot log; the folder is printed as `Boot evidence: logs/device/<random>/`.
  Read it when the bug is in early boot. For a bug in the boot or Wi-Fi association itself, start the
  logger first, then ask the user to press the kit's reset button so the whole
  boot is captured.
- If `make` is missing, use the direct commands in
  [self-checks.md](../device-firmware-guardrails/self-checks.md). If `.venv` is
  missing, stop and ask the user to run `make install`.

## Phase 1 — Intake

Collect, asking only for what is missing (use AskQuestion for choices):

| Item | Why |
|---|---|
| Symptom in the user's words | What "broken" looks like |
| Exact reproduction steps (buttons, screens, order) | You will hand these back to them every iteration |
| Expected behavior, with the spec row if known | `docs/product/experiences/…`, `docs/product/device-ui/…` |
| Frequency (always / sometimes / after N tries) | Decides how many reproductions an iteration needs |
| Last known good commit or build | The baseline for diffs and memory comparison |
| Kit nickname, and whether it is plugged in now | Needed for flash and log |
| What previous agents changed for this bug | So you do not repeat a failed fix |

Then run `make devices` (or `.venv\Scripts\python.exe scripts\firmware.py devices`)
to confirm the kit is connected.

## Phase 2 — Baseline

1. `git status`, current branch, `git log --oneline -5`.
2. `git diff --stat <last-good>` to see what changed since it worked.
3. Read `artifacts/device/bootstrap/current.json` for the current build id. If
   you cannot tell whether the kit runs HEAD, build and flash HEAD first so the
   first reproduction matches the code you are reading.
4. Read the code on the failing path, end to end: the tick phase, the service,
   the worker, and the controller involved.

## Phase 3 — Hypotheses (no product edits yet)

Write 3–5 ranked hypotheses. For each one give:

- **H<n>**: the suspected cause, in one sentence, with file and function.
- **Invariant**: the guardrail or standard ID it would violate (G1–G9,
  PRES-*, MEM-*, STK-PATH, BOOT-ORDER) or "none".
- **Prediction**: what the log will show if it is true, and what it will show
  if it is false. A hypothesis with no distinguishing log signature needs
  different instrumentation, not a fix.

Cheap evidence first. Before adding logs, check whether existing output already
answers it: `V2_MEM` lines (with `CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC=y`),
Wi-Fi disconnect reasons from `product_network`, `Guru Meditation`, `abort()`,
`stack overflow`, `task_wdt`, `assert failed`, and `E (` error lines.

For connection, TLS, or "it got worse" problems, the first hypothesis to test
is internal-heap pressure (guardrail G3/G5). Compare `V2_MEM` internal free and
largest internal block at `ui_ready` against the last good build before
anything else.

Present the hypotheses and the instrumentation plan to the user in a few
lines, then continue. Do not wait for approval unless the plan needs a config
change or touches files outside the failing path.

## Phase 4 — Instrument

Add temporary logging that distinguishes the hypotheses:

- Format: `ESP_LOGI(TAG, "DBG H<n> <event> key=value ...")`. Every debug line
  contains `DBG H` so it can be found and removed with one grep.
- On the LVGL task, log **edges, not ticks**: state changes, first
  occurrence, or a counter summary at most once per second. A 20 Hz log line
  changes timing and can hide the bug.
- No allocation, no blocking, no SD or codec calls in instrumentation (G2).
- Memory questions use `product_mem::probe("dbg_<phase>", detail)` with the
  diagnostic flag, not ad hoc heap calls.
- Keep a list of every instrumented file and line in the ledger.
- Run `make test` (host build catches typos), then `make app:build KIT=<kit>`.

## Phase 5 — Capture a reproduction

1. Stop any running logger (command above).
2. Flash: `.venv\Scripts\python.exe scripts\app_host.py flash --kit <kit>`.
   Confirm the output contains `PASS <kit>` and the build id you just built.
3. Start the logger in the background with a new `--out` file.
4. Give the user the reproduction steps as a short numbered list, then ask
   with AskQuestion, for example:
   - "Reproduced: the bug happened"
   - "Did not reproduce: it worked this time"
   - "Something different happened" (they type what)
   - "Device rebooted / screen went dark"
5. **End your turn and wait.** Do not read the log until they answer.

## Phase 6 — Read the log and decide

1. Grep the log for `DBG H`, then `Guru|abort|panic|overflow|wdt|assert`, then
   `E \(`, then `V2_MEM`, then `serial lost`.
2. For each hypothesis write **confirmed**, **refuted**, or
   **inconclusive**, quoting the log lines (with timestamps) that decide it.
3. Decide:
   - One hypothesis confirmed → Phase 7.
   - All refuted or inconclusive → new or sharper hypotheses, more targeted
     instrumentation, back to Phase 4. After three capture rounds with no
     confirmed cause, stop and review with the user what you have learned
     and what you would try next.
   - Did not reproduce → ask for a second attempt, or add instrumentation on
     the timing or ordering that could make it intermittent.

## Phase 7 — Fix one cause

1. Change the smallest thing that removes the confirmed cause, inside the
   guardrails. Prefer copying the Slice 1/2 reference pattern over inventing a
   new mechanism.
2. If the root cause is reachable on host, add a test that fails without the
   fix (`scripts/tests/view_controller_host/` or the shared model tests).
3. Keep the DBG instrumentation in place for verification.
4. Run `make test`, `make app:test`, and `make app:build KIT=<kit>`. Paste
   failures; do not flash a build whose tests fail.
5. If the fix needs a spec change, a new task, a config change, or a file
   outside the failing path, stop and ask (guardrails "Stop and ask").

## Phase 8 — Verify on the device

1. Stop the logger, flash, start a new log file (Phase 5 steps 1–3).
2. Ask the user to run the **original reproduction**, then a short regression
   list chosen for the code you touched. The default list:
   - boot → roster → PIN → signed in
   - mute → unmute → sign in again
   - the journey the fix lives in (for example inbox Play/Pause, or record → send)
   - the journey next to it that shares a service or worker
3. Wait for their answer (AskQuestion), then read the log.
4. Compare before and after with quoted lines: the failing signature is gone,
   the expected path appears, and no new `E (`, panic, or watchdog lines exist.
5. If the user says it is still broken or something else broke: record the
   attempt as failed in the ledger, revert the fix unless it fixed a separate
   proven defect, and return to Phase 3 with the new evidence.

Show the ledger after every capture:

```text
Session: <bug>   Kit: <kit>   Baseline: <sha>
| Iter | Build id | Change flashed               | Log file            | Result (user)   | Log verdict              |
|------|----------|------------------------------|---------------------|-----------------|--------------------------|
| 1    | 4f2a…    | DBG H1–H3 instrumentation    | …/bug-1.log         | Reproduced      | H2 confirmed (12:01:07)  |
| 2    | 7c1e…    | Fix H2: gate playback on user| …/bug-2.log         | Works; regr. ok | DBG H2 path absent       |
Hypotheses: H1 refuted · H2 confirmed · H3 untested
Instrumented: product_runtime.cpp:212, product_playback_service.cpp:88
```

## Phase 9 — Close

Only after the user confirms and the log agrees:

1. Remove all instrumentation: `git grep -n "DBG H" -- client` prints
   nothing. Revert any diagnostic Kconfig flag you enabled.
2. Rebuild, run `make test`, and flash once more if the user wants the clean
   build on the kit.
3. Run the self-checks in
   [self-checks.md](../device-firmware-guardrails/self-checks.md).
4. Report: root cause (one sentence, with the deciding log line), the fix and
   files, tests added, the final ledger, and the claim class reached
   (normally **behavior**, because the user ran the journey with a saved log).
   Do not commit unless the user asks.

## When there is no kit

If no kit is connected or the user cannot reproduce right now, say so, finish
Phases 1–4 (hypotheses and instrumentation ready to flash), and give the user
the exact flash and log commands to run later. Keep the claim class at
**compile** at most.
