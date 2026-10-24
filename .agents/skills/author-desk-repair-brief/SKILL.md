---
name: author-desk-repair-brief
description: >-
  Expand operator desk PASS/FAIL notes and server telemetry into copy-paste repair
  prompts for client/device work. Pulls governing experiences, contracts, standards,
  device-target-invariants, verification ladder, and test expectations. Use when
  the user has a small UX test failure, wants a prompt for a fix agent, or asks
  for Wave/campaign handoff text. Does not implement firmware; pair with
  client-code-review on the repair chat.
disable-model-invocation: true
---

# Author desk repair brief

Turn **short operator desk results** into a **complete repair-agent prompt**.
This skill is **prompt engineering and traceability only** unless the user asks
to save a durable note under `docs/plans/`.

**Pairing**

| Goal | Skill on that chat |
|------|---------------------|
| Write the prompt | **author-desk-repair-brief** (this skill) |
| Run review / fix / verify | **client-code-review** on a **separate** chat |
| Durable product retro after dogfooding | **capture-product-feedback** → `docs/plans/<bundle>/retro.md` |
| Cluster post-mortem across many failures | **deep-implementation-retrospective** (not the fix loop) |

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
| **Target chat** | New agent vs continue existing (paste transcript id if continue) |
| **Mode** | Prompt only vs also save `docs/plans/<bundle>/repair-brief.md` |

## Procedure

### 1. Classify scope

Pick **one primary experience** per repair prompt. Others are **boundary-only**
(cite §1 handoff rows; do not scope full fixes).

| Symptom area | Primary spec | Typical boundaries |
|--------------|--------------|-------------------|
| Mute, roster, PIN, connecting | `docs/product/experiences/endpoint-login.md` | Send mute during finished send; carousel entry after auth |
| Record, send, picker, red from inbox | `docs/product/experiences/message-recording-and-sending.md` | Login PIN/roster; carousel red handoff only |
| Inbox play, sketch on messages, carousel | `docs/product/experiences/message-carousel-and-playback.md` | Red exit into send; login connectivity copy |

**Campaign order** (default): login desk survival → record/send → carousel L3.
Do not start Wave 2 until Wave 1 exit bullets pass on the kit.

### 2. Build applicable sources (read before writing the prompt)

Read only what the failure touches. Record path + revision in the brief.

| Layer | Start here |
|-------|------------|
| Experience behavior | `docs/product/experiences/<primary>.md` |
| Wire / HTTP / operation keys | Matching file under `docs/product/communication-contracts/` (login → `endpoint-health-and-login.md`, send → `message-delivery.md`) |
| Engineering process | `docs/standards/client-code-review-agent.md`, `docs/standards/client-application-coding-standards.md` |
| Embedded profile + claim classes | `docs/standards/device-target-invariants.md` |
| Build / flash / markers | `docs/standards/device-build-flash.md`, `client/device/README.md` |
| Agent boundaries | `docs/AGENTS.md` |

Map symptoms to **contract rows** when possible (e.g. COM-006 PIN key,
COM-015 `identity_conflict`, COM-014 `operation_expired`, COM-020 HTTP map).

### 3. Diagnose (hypothesis, not implementation)

For each FAIL, state in plain language:

- **Observable** — what the operator saw
- **Likely server outcome** — from telemetry if present
- **Likely client state** — stage, operation_id reuse, refuse vs wrong_pin path
- **Claim class** — tooling / compile / flash / boot-marker / behavior (from device-target-invariants)
- **Open invariant** — PRES-PATH, INT-WIRE, etc., only if relevant

Separate **brick** (no recovery) from **workaround** (mute, reboot). Do not
treat workarounds as spec success.

### 4. Define exit criteria (L3 bullets)

Each repair prompt must end with **3–5 testable desk bullets** and when Wave
closes. Example pattern:

- Wrong PIN → header `wrong pin` ~2s, retry on PIN, **200 `wrong_pin`**, not 409 on first submit
- Second PIN attempt → inbox without mute
- `make test` after each fix; name model tests if login/send (`test_product_login_model.py`)

### 5. Emit two deliverables

#### A) Operator summary (short)

- Scope: primary EXP + boundaries
- Hypothesis table (FAIL → cause → next proof)
- What **not** to scope (other waves, unrelated `make test` buckets)
- Whether to **restart desk server** (stale `pin_records`) or flash new firmware

#### B) Copy-paste **repair agent prompt** (long)

Use the template in [prompt-template.md](prompt-template.md). Always include:

- `Mode:` from client-code-review (usually **Review, implement, and verify**)
- `Target: client/device/`
- Primary governing experience + revision
- Boundaries paragraph
- **Desk evidence** block (user’s PASS/FAIL verbatim + telemetry)
- **Governing sources** list (paths only)
- **Tasks** ordered, one defect at a time; subagent rules from client-code-review
- **Verification**: `make test`, named unit tests, L3 desk + serial or waiver
- **Hard outs**: no `poc-v1/`, no parallel implementers, no “aligned with experience” without L-level
- **Wave exit** checklist

If continuing an existing chat, add: prior finding IDs, commits, what is already PASS.

### 6. Optional persistence

When the user asks to save: create or update
`docs/plans/<bundle>/repair-brief.md` with sections **Evidence**, **Hypotheses**,
**Prompt (latest)**, **Wave status**. Link from bundle `README.md`. Do not commit
unless requested.

## Quality bar before finishing

- [ ] Primary experience is singular; boundaries named
- [ ] At least one contract or EXP row cited per FAIL
- [ ] Claim class stated for “done”
- [ ] Repair prompt is self-contained (another chat needs no scrollback)
- [ ] Tests named: host model, `make test`, desk L3
- [ ] User told to attach **client-code-review** on the **repair** chat, not this one

## Reference

- Full checklist and failure→contract hints: [reference.md](reference.md)
- Filled example (login PIN wave): [examples.md](examples.md)
