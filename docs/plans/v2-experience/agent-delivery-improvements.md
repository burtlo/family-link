# Improving agent delivery on device firmware

Date: 2026-10-30
Status: Proposal for owner review
Evidence: slice 3 and 4 definitions, commit `67d0325` on `slice-3-4-grok`, and
the review in `slice-3-4-repair-brief.md` (commit `ba1e520`)

## Summary

The slice 3/4 failure was mostly a process failure, not only a model failure.
The model was given outcome-level slice definitions and was allowed to make
the engineering decisions those documents explicitly deferred. It implemented
two slices in one pass, with no frozen package briefs, no independent
verification, and no target measurement. It then marked the work as done.

Slices 1 and 2 succeeded with the same kind of documents because each had an
implementation map, one package dispatched at a time, a fresh read-only
verifier, and a progress log. Slices 3/4 skipped all four.

The highest-leverage changes, in order:

1. Never dispatch a slice definition directly. Dispatch only frozen package
   briefs that name allowed files, the seams to reuse, target facts, forbidden
   operations, and exit checks.
2. Turn the guardrails into checks that fail the build, so compliance does not
   depend on the model reading and remembering prose.
3. Make the host test harness detect the target's failure modes: file I/O,
   codec calls, blocking locks, and allocation on the LVGL tick, plus work
   that runs before sign-in.
4. Require a measured memory comparison against the baseline before any
   package that adds buffers, tasks, or workers counts as complete.
5. Size the model and the review to the risk. Constrained firmware work needs
   a stronger planner and a verifier from a different model family.

## What went wrong, and why the process allowed it

The repair brief lists 17 findings. Most of them trace back to a few gaps in
what the agent was given.

| Failure (repair brief finding) | Gap that allowed it |
|---|---|
| Boot-time capture and renderer allocation in internal SRAM; silent PSRAM → internal fallback (F1, F11) | The slice docs said to place memory "explicitly" but did not state the 16 KiB `SPIRAM_MALLOC_ALWAYSINTERNAL` threshold or the internal reserve shared with Wi-Fi. A plain `malloc` looked correct to the agent. |
| Wi-Fi changes (F2) | No rule against fixing symptoms in other subsystems, and no memory measurement that would have pointed at the real cause. |
| Playback reset on every generation change; work before sign-in (F3, F4) | Login `generation` semantics were not documented anywhere the agent read. The name suggests "session identity". |
| SD, hashing, and codec I/O on the LVGL tick; `portMAX_DELAY` lock (F5, F7, F8, F10) | "Bounded worker" and "non-blocking" were stated as goals, not as banned calls. Host tests cannot observe blocking. |
| Second HTTP task, second codec handle, second owner of `flmsgs.bin` (F6, F7, F13) | The slice 3 data flow said "bounded fetch/storage/decode worker" without naming the existing inbox worker. The agent read that as permission to add one. |
| Neighbor ink caches and per-point `lv_draw_rect` (F9) | The spec conflict ("a paused visible card must not revert to its portrait" versus "one live surface") was never surfaced as an owner decision. The agent resolved it by allocating. |
| Identity written outside the atomic commit (F12) | "Commit atomically enough" left the commit boundary to the agent, without a torn-write test that was required to fail first. |
| Spec rows rewritten; Status lines set to "implemented" (F16) | Nothing prevented writes to `docs/product/` or Status lines. Nothing required evidence for a claim. |
| Reformatting of slice 1/2 files; mangled CMake (F16, F17) | No diff-scope check. `make format` was run across files the agent was not changing. |
| No per-package verification or target measurement | Both slices were delivered in one commit. The `orchestrate-build` loop and the S3-P5/S4-P6 target gates were not used. |

Three patterns stand out:

- **Soft language was read as permission.** Phrases such as "or equivalently",
  "when safe", "where appropriate", "compare … before declaring final", and
  "verify actual legacy allocation" all delegate a decision. A model under
  completion pressure takes the cheapest reading.
- **Target facts lived in people's heads.** The repair brief needed a whole
  section of background facts (thresholds, generation semantics, legacy
  cancellation paths, which worker already does what). Every one of those
  facts would have prevented a P0 or P1 finding if it had been in the brief.
- **Host-green was treated as done.** The tests passed and the firmware
  built, but the device could not connect. The test harness had no way to
  detect the failure classes that matter on this target.

## Recommendations

### 1. Require a frozen implementation map before dispatch

The slice 3 and 4 documents describe outcomes. Both say current-source
planning is required before implementation. Make that a hard gate:

- Run `/spec-implementation-map` (or an equivalent planning pass) for each
  slice. Produce one package brief per package in the format the repair brief
  uses: allowed files, steps, stop-and-ask conditions, exit checks, and a
  report template.
- Every brief names the **existing seam** for each concern, using the
  reference table in the `device-firmware-guardrails` skill. For example:
  "media downloads go through `product_inbox_http_worker` as a new job kind",
  never "a bounded worker".
- Every brief lists its **open owner decisions** before dispatch. Example:
  how a visible, paused, non-selected card should look. An implementer must
  not receive a brief with an unresolved product conflict.
- Dispatch only through `/orchestrate-build`: one package per implementer
  run, a fresh verifier, a progress file, and a stop between packages. One
  slice per branch. Never dispatch two slices in one run.

The repair brief itself is the template to copy. It is the most effective
document in this effort because it removes choices.

### 2. Put target facts into every firmware brief

Add a short, maintained **target facts** section to
`docs/standards/device-target-invariants.md` (or a sibling page), and
require every firmware brief to include or link it. It must cover at least:

- the 16 KiB internal-allocation threshold, the 64 KiB internal reserve, the
  `.bss` and `xTaskCreate` stack placement rules, and the 32 KiB LVGL pool;
- that login `generation` is a stale-result tag that changes on every stage,
  not a sign-in identity;
- the list of existing workers and hardware owners, and what each one
  already does (for example, incremental SHA-256 for media slices in the
  inbox worker);
- legacy code that still reaches global state (for example, legacy inbox
  media job kinds that consult `CarouselSession`, and `RecordSession`
  allocating its renderer at `boot()`).

Before a brief reuses legacy code, the planner adds a **legacy reuse audit**:
what that code allocates, which tasks and handles it creates, and when. Slice
3 asked the implementer to do this. Move it to the planner, where it can be
reviewed.

### 3. Write briefs without optional language

Briefs are instructions, not design discussions. When drafting or reviewing
a package brief, replace delegating phrases:

| Instead of | Write |
|---|---|
| "a bounded worker" | the named existing worker and job kind |
| "or equivalently an explicit owner" | the one owner, by type name |
| "when safe" / "where appropriate" | the condition, or a stop-and-ask |
| "explicit memory placement" | the region, the allocator call, and "no fallback" |
| "compare before declaring final" | an exit check with the measurement and pass threshold |
| "atomic enough" | the commit point and the torn-write tests that must fail first |

Keep the outcome-level slice documents as they are. This rule applies to
the briefs derived from them.

### 4. Enforce the guardrails mechanically

Prose rules were already in the slice docs, and they were broken. The
`device-firmware-guardrails` skill and `self-checks.md` are a good
statement of the rules, but they still depend on the agent running and
honoring them. Move as much as possible into checks that fail:

- **A guardrail check script** (for example
  `scripts/check_device_guardrails.py`, run by `make test`) that encodes the
  greps in `self-checks.md`:
  - no `fopen`/`fread`/`fwrite`/`std::remove`/`hashFile` in tick-reachable
    files;
  - one `bsp_audio_codec_speaker_init` call site;
  - an allowlist of `xTaskCreate*` call sites;
  - no `MALLOC_CAP_INTERNAL` retry after a PSRAM request;
  - no `vTaskDelay(1)` loops;
  - one owner of each media file path.
  Changing an allowlist then shows up in the diff, where the owner reviews it.
- **A diff-scope check** that compares changed files with the package's
  allowed list and reports any extras. It should also report changes under
  `docs/product/`, `poc-v1/`, `sdkconfig*`, `product_network.cpp`, and any
  `Status:` line under `docs/plans/`. The orchestrator runs it before
  accepting a package.
- **A Cursor hook** (the `create-hook` skill) that refuses agent writes to
  `docs/product/**` and `poc-v1/**` unless the owner asked for them. Rules
  that can be enforced at write time should not rely on the model.
- **Formatting scoped to changed lines.** Provide a `make format.changed`
  target and tell agents to use it instead of `make format`.

### 5. Make the host harness see target failure modes

The host suite passed while the device was broken. Add permanent fixtures to
`scripts/tests/view_controller_host/` so these failures fail a test:

- **Tick sentinel.** Fake file, codec, and lock layers that fail the test when
  called during `ProductRuntime::tick`, `render()`, or an LVGL event callback.
- **Pre-login run.** Drive boot → roster → PIN and assert zero feature-service
  jobs, storage operations, and codec calls. Keep this test for every future
  slice.
- **Counting allocator.** Assert that construction allocates nothing large,
  that leases return live bytes to zero, and that Inbox and Sketch pixel
  buffers never coexist.
- **LVGL pool monitor.** Assert pool usage stays flat across many ticks of
  playback and drawing.
- **Lifecycle journeys.** Sign in → play → mute → unmute → sign in → play, and
  the same for record/send. Several P0 findings appeared only on the second
  session.

Once these fixtures exist, a new package brief can require "the sentinel
suite passes" instead of hoping the implementer writes such tests.

### 6. Gate completion on target evidence

Use the claim classes in `device-target-invariants.md` as gates, not
vocabulary:

- A package that adds a buffer over 1 KiB, a task, a worker job kind, or a
  hardware handle is not complete until there is a saved monitor log with
  `V2_MEM` readings at `ui_ready` and after Wi-Fi association. Those readings
  must be no worse than the baseline commit for internal free and largest
  internal block.
- The orchestrator records the claim class reached for each package in the
  progress file. Only the owner changes a slice `Status:` line.
- Commit messages and documents use the claim class ("compile", "flash"),
  not "delivered".

This single check would likely have caught F1, whose effect looked like a
Wi-Fi problem, before anyone touched Wi-Fi code.

### 7. Match the model and the review to the risk

The evidence here is one run, so treat this as a hypothesis to test, not a
verdict on a model:

- **Planning** (implementation maps and briefs) uses the strongest available
  reasoning model. Planning is where the decisions were lost.
- **Implementation** of firmware packages that touch memory, tasks, storage,
  or the tick uses a high-reasoning setting. Medium reasoning is fine for
  narrow, fully specified packages such as a view controller with a fixed
  layout.
- **Verification** uses a different model family from the implementer, with
  a fresh context. The verifier checks the brief **and** the guardrails, so it
  catches out-of-scope edits the brief did not anticipate.
- **Benchmark.** The repair brief is a ready-made evaluation. Run R1–R3 with
  two or three candidate models in isolated worktrees (`best-of-n-runner`),
  and compare guardrail-check failures, stop-and-ask quality, and exit-check
  honesty. Use that result to choose models for later slices.

### 8. Make stopping a successful outcome

The agent edited specs and Status lines instead of asking. Change the
incentives in prompts and reports:

- The orchestrator treats `blocked` with evidence and options as a good
  result and reports it as such.
- Every brief lists its stop-and-ask conditions explicitly, as the repair
  brief does.
- The implementer report includes "files that looked relevant but were not
  changed". This makes out-of-scope temptations visible.
- Briefs include a budget: if two fixes do not change device behavior, stop
  (guardrail G8).

### 9. Feed every review back into the rules

The review became the guardrails skill within the same day. Keep that loop:

- After each slice, run a short retrospective (`deep-implementation-retrospective`)
  and add new failures to the guardrails as rules with a "Previously" line.
- Add a mechanical check (recommendation 4) or a harness fixture
  (recommendation 5) for each new rule wherever possible. A rule with only
  prose will be broken again.

## Proposed next steps

| Step | Artifact | Depends on |
|---|---|---|
| Add target facts page | `docs/standards/` | — |
| Guardrail check script and allowlists | `scripts/`, wired into `make test` | — |
| Diff-scope check and write-protection hook | `scripts/`, `.cursor/hooks.json` | — |
| Host harness fixtures (sentinel, pre-login, allocator, pool) | `scripts/tests/view_controller_host/` | — |
| Update `orchestrate-build` to require target-facts, diff-scope, and claim class per package | `.cursor/skills/orchestrate-build/` | the checks above |
| Model benchmark on repair packages R1–R3 | isolated worktrees | check script |
| Repair `slice-3-4-grok` using the repair brief under the new loop | branch | all of the above |

## How to tell whether this works

Track these per package in the orchestration progress file:

- P0/P1 review findings after the verifier passes (target: zero);
- guardrail-check and diff-scope failures on the first implementer attempt;
- stop-and-ask events, and whether each was justified;
- claim class reached compared with claim class asserted;
- internal free and largest internal block compared with the baseline.
