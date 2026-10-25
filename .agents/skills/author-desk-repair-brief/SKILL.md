---
name: author-desk-repair-brief
description: >-
  Turn desk PASS/FAIL notes and server telemetry into a structured repair session:
  scope, hypotheses, measurement plan, and ordered tasks—then troubleshoot and
  fix in the same chat when the user is ready. Pulls experiences, contracts,
  device-target-invariants, and verification ladder. Use for UX test failures,
  Wave desk campaigns, record/send transition crashes, or memory/timing doubts.
  Optional save under docs/plans/. Follow client-code-review for implement/verify.
disable-model-invocation: true
---

# Author desk repair brief

Turn **short operator desk results** into a **session plan** you can see in chat,
then **investigate and fix in this conversation** when you choose—no required
handoff to a second chat.

Optional: save durable notes under `docs/plans/<bundle>/repair-brief.md`.

**Related skills**

| Goal | Skill |
|------|--------|
| This brief + same-session repair | **author-desk-repair-brief** (this skill) |
| Review/fix discipline only | **client-code-review** — read `docs/standards/client-code-review-agent.md` while working here |
| Product retro after dogfooding | **capture-product-feedback** |
| Cluster post-mortem | **deep-implementation-retrospective** |

Do not read or cite `poc-v1/`. Positive patterns live under `client/labs/` and
`docs/`.

## What the user supplies (minimal)

Ask only for missing items. Accept bullet lists.

| Field | Required |
|-------|----------|
| **PASS / FAIL steps** | Yes — numbered, screen names, button names |
| **Primary journey** | Infer if omitted: login / record-send / carousel |
| **Server telemetry** | Strongly encouraged for login/send (JSON lines) |
| **Workarounds** | e.g. mute recovery, power cycle |
| **Wave / campaign** | e.g. Wave 1 login, Wave 2 record-send |
| **Start mode** | Plan only vs **investigate/fix in this session** (default: plan then invite) |
| **Persistence** | Optional `docs/plans/<bundle>/repair-brief.md` |

Remove **target chat** / copy-paste prompt workflow—the session continues here.

## Procedure

### 1. Classify scope

Pick **one primary experience** per repair session. Others are **boundary-only**
(cite §1 handoff rows; do not scope full fixes).

| Symptom area | Primary spec | Typical boundaries |
|--------------|--------------|-------------------|
| Mute, roster, PIN, connecting | `docs/product/experiences/endpoint-login.md` | Send mute during finished send; carousel entry after auth |
| Record, send, picker, red from inbox | `docs/product/experiences/message-recording-and-sending.md` | Login PIN/roster; carousel red handoff only |
| Inbox play, sketch on messages, carousel | `docs/product/experiences/message-carousel-and-playback.md` | Red exit into send; login connectivity copy |

**Campaign order** (default): login desk survival → record/send → carousel L3.
Do not start Wave 2 until Wave 1 exit bullets pass on the kit.

### 2. Build applicable sources (read before planning)

Read only what the failure touches. Record path + revision in the brief.

| Layer | Start here |
|-------|------------|
| Experience behavior | `docs/product/experiences/<primary>.md` |
| Wire / HTTP / operation keys | Matching file under `docs/product/communication-contracts/` |
| Engineering process | `docs/standards/client-code-review-agent.md`, `docs/standards/client-application-coding-standards.md` |
| Embedded profile + claim classes | `docs/standards/device-target-invariants.md` |
| LVGL / memory framing | `docs/product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md` |
| Build / flash / markers | `docs/standards/device-build-flash.md`, `client/device/README.md` |
| Agent boundaries | `docs/AGENTS.md` |

Map symptoms to **contract rows** when possible (e.g. COM-006 PIN key,
COM-015 `identity_conflict`, COM-014 `operation_expired`, COM-020 HTTP map).

### 3. Diagnose (hypothesis, not implementation)

For each FAIL, state in plain language:

- **Observable** — what the operator saw
- **Likely server outcome** — from telemetry if present
- **Likely client state** — stage, operation_id reuse, refuse vs wrong_pin path
- **Failure class** — MEM-CAP, MEM-LEDGER, PRES-PATH, INT-WIRE, etc. from device-target-invariants when embedded; otherwise coding-standards class
- **Claim class** — tooling / compile / flash / boot-marker / behavior
- **Open invariant** — only if relevant

Separate **brick** (no recovery) from **workaround** (mute, reboot). Do not
treat workarounds as spec success.

#### 3a. Multi-resource peaks (record / send / transition)

When the FAIL happens around **wipe, draw, RED, collapse, or receipt**, do not
collapse to “transition bug” or “LVGL pool” without checking overlap:

| Resource | Typical product involvement |
|----------|-----------------------------|
| **Presentation tick** | Single LVGL timer: buttons, record `paint_motion`, send stage, present |
| **SD** | `finish_capture` commit on RED (sync on button path); upload reads finished object |
| **Wi‑Fi / HTTP** | Workers off-tick; scheduling and read callbacks still interact with timeline |
| **Touch / ink** | Active until RED; not during `finish_reverse` |
| **Ledger A vs B** | Ring/masks vs pool; inbox/record **pixels** mostly PSRAM |

List **competing resources** in the session plan. If failure class is unclear,
**task 1 is measurement**, not sdkconfig changes.

### 4. Define exit criteria (L3 bullets)

End the session plan with **3–5 testable desk bullets** and when the Wave
closes.

### 5. Deliver in chat (same session)

#### A) Short operator summary

- Scope: primary EXP + boundaries
- Hypothesis table (FAIL → cause → next proof)
- What **not** to scope
- Desk ops: restart server vs flash firmware vs mem_diag

#### B) Full **session plan**

Render using [session-plan.md](session-plan.md) (headings in chat, not a
“paste into new agent” block).

#### C) Invitation — then wait or proceed

Close with a clear invite, tuned to user intent:

**Default (plan first):**

> This is the scope and order I’ll follow. Say **go**, **start**, or **measure
> first** when you want me to run investigation / fixes in this chat.

**If the user already asked to fix or debug:**

> Starting with task 1: {{…}}. I’ll follow the plan above and report evidence
> before any MEM-LEDGER / partition change.

**Plan-only:** If the user asked only for a brief, stop after (A)+(B)+(C) without
editing product code.

#### D) Execute (when started)

Follow **client-code-review** modes in this session:

- Unclear root cause → investigate (build, flash, monitor, saved serial, host
  models) before implementation.
- Confirmed defect → one fix at a time, `make test`, independent verify.
- Device memory fixes → MEM-LEDGER / MEM-DELTA rules; document artifact or waiver.

Run commands and read serial yourself; do not only advise the operator unless
blocked (no kit, no flash permission).

### 6. Optional persistence

When the user asks to save: `docs/plans/<bundle>/repair-brief.md` with
**Evidence**, **Hypotheses**, **Session plan (latest)**, **Wave status**. Link
from bundle `README.md`. Do not commit unless requested.

## Quality bar before inviting “go”

- [ ] Primary experience is singular; boundaries named
- [ ] At least one contract or EXP row cited per FAIL
- [ ] Failure class and claim class stated for “done”
- [ ] Record/send peaks: concurrent resources listed or N/A
- [ ] Measurement plan present before memory/partition tasks
- [ ] Tasks ordered; first task is investigate when class unproven
- [ ] Wave exit bullets listed
- [ ] User invited to start in **this** session (unless plan-only)

## Reference

- Failure→contract hints and device peaks: [reference.md](reference.md)
- Example shape: [examples.md](examples.md)
- Legacy copy-paste prompt: [prompt-template.md](prompt-template.md) (deprecated;
  use session-plan.md)
