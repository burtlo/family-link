# REPORT — LAB-02 (template)

Replace `<rev>` and fill all sections before publishing as `REPORT-LAB-02-<rev>.md`.

## Lab version

`LAB-02/<rev> = firmware <build-id> + server <none|rev> + fixture inbox-summary/<fixture-rev>`

## Hardware and toolchain

- Board:
- Kit alias:
- IDF:
- Display / LVGL profile:

## Scenarios

| ID | Level | Pass / fail / inconclusive | Notes |
|----|-------|----------------------------|-------|
| S-fix-8 | | | |
| … | | | |

## Target (pre-run gates)

Document numeric gates chosen **before** measurement. Do not use v1 demo PASS or unreviewed draft budgets as approved product limits.

## Measured

Link to `runs/<run-id>/metrics.json` and summarize p50/p95/p99/worst. Include eager vs recycle comparison when `S-eager-16` ran.

## Review triggers

Which triggers from the [inbox plan § LAB-02](../../inbox-carousel-playback-evaluation.md#lab-02--bounded-inbox-and-card-recycling) fired?

## Decision

| Topic | Outcome | Evidence |
|-------|---------|----------|
| Card controller | OPEN / recommended … | run-id |
| Inbox paging | OPEN / recommended … | run-id |

Do not approve OPEN communication-contract items by assertion.

## Verification summary

Highest level achieved per scenario (L0–L3).
