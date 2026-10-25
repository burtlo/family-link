# In-chat session plan template

Use this structure **in the conversation** (markdown headings, not a fenced
copy-paste block for another chat). Replace `{{…}}`. After presenting it, use
the **invitation** closing from [SKILL.md](SKILL.md) §5.

## Session plan

**Mode:** {{Review only | Review, implement, and verify | Investigate then fix}}

**Target:** `client/device/` (and `server/device/` only if wire evidence requires it)

**Primary governing experience:** `docs/product/experiences/{{experience-slug}}.md`
({{EXP-ID}}, rev {{N}})

**Boundaries only (not full fixes here):**

- {{other experience / handoff}}

**Wave:** {{N}} — {{campaign name}}

### Desk evidence

**PASS:**

- {{…}}

**FAIL:**

- {{…}}

**Workarounds:**

- {{…}}

**Server telemetry** (if any): summarize or paste JSON lines.

### Interpretation

| FAIL | Observable | Hypothesis | Failure class | Next proof |
|------|------------|------------|---------------|------------|
| {{…}} | {{…}} | {{…}} | {{MEM-* / PRES-PATH / CLAIM / COM-*}} | {{measurement or serial}} |

### Concurrent resources (device only — when relevant)

State what overlaps at the failure moment (check all that apply):

- **Presentation tick** — LVGL timer, buttons, `paint_motion`, transitions
- **SD** — mount, `finish_capture` commit, upload read from `flmsgs.bin`
- **Wi‑Fi / HTTP** — inbox/login/send workers vs collapse timing
- **Touch / draw** — capture active until RED
- **Ledger A** — LVGL pool / masks (not carousel PSRAM canvases)

**Measure before memory/partition fixes** unless serial already proves the class:

| Phase | What to capture |
|-------|-----------------|
| Boot | `CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC`: `before_display` / `after_display` |
| Repro peak | Wipe start, RED, finish reverse, first HTTP submit |
| Metrics | `dma_largest_internal`, `lv_mem_monitor`, optional PSRAM largest |
| Saved log | `desk_record_send_check.py` on serial file |

**Decision rule:** Do not change `CONFIG_LV_MEM_SIZE`, draw height, or factory
partition size until the table above supports **MEM-LEDGER** / **MEM-DELTA** or
the user waives with an explicit artifact path.

### Governing sources (read as needed)

- `docs/standards/client-code-review-agent.md`
- `docs/standards/client-application-coding-standards.md`
- `docs/standards/device-target-invariants.md`
- `docs/product/experiences/{{primary}}.md`
- `docs/product/communication-contracts/{{contract}}.md`
- `client/device/README.md`

### Workflow (this session)

- Standards-aligned review when implementing; one confirmed defect at a time.
- Fix → independent verification (fresh read or subagent per client-code-review).
- `make test` after each fix; unrelated failures cited, not drive-by fixed.
- Max claim class and artifact per finding (device-target-invariants).

### Tasks (ordered)

1. {{Investigate / measure — first when failure class unclear}}
2. {{First defect — reproduction, files, acceptance}}
3. {{…}}

### Hard outs

- No `poc-v1/` edits or links.
- No {{Wave N+1}} scope without ordering.
- No behavior / L3 claim without desk serial or waiver.
- No “transition-only” fix for record/send RED without checking SD + timing on
  presentation path.

### Wave exit (L3)

- {{3–5 testable desk bullets}}

**First action when you say go:** {{e.g. enable mem_diag + one repro | host model
test for COM-015 | read timer path for PRES-PATH}}
