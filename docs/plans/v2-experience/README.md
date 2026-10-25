# V2 core journey

Status: **preview** (published 2026-10-22; lab inventory refreshed the same day for `LAB-01` through `LAB-14`; contract reconciliation the same day).

Firmware exists for `LAB-02` through `LAB-12` and `LAB-14`. `LAB-01` and `LAB-13` have no project trees. Those labs are evidence for the map. They are not the product client. `LAB-12` stays outside the slice.

This bundle maps the core V2 journey: login, inbox and playback, record/send, and Volume. Profile editing and the settings hub are out of scope. Experience specifications under `docs/product/experiences/` are the behavior source. There is no `spec.md` or `retro.md` here.

The map does not authorize firmware work. Core-journey wire decisions are recorded in the contracts. The map becomes ready for orchestration only after this preview is accepted and **P1** verifies a fixture for those revisions. Decision acceptance alone does not finish P1.

## Artifacts

| File | Purpose |
|------|---------|
| [implementation.md](implementation.md) | **Canonical** — deltas, work packages (P1–P10), verifier hooks, build order |
| [ui-memory-lifecycle-review-2026-10-25.md](ui-memory-lifecycle-review-2026-10-25.md) | Read-only review of memory ownership, LAB-16 measurement coverage, and scene lifecycle |

## Normative sources

| Experience | Spec |
|------------|------|
| Signed-out login | [endpoint-login.md](../../product/experiences/endpoint-login.md) |
| Inbox and playback | [message-carousel-and-playback.md](../../product/experiences/message-carousel-and-playback.md) |
| Sketch geometry | [sketch-record-and-playback.md](../../product/experiences/sketch-record-and-playback.md) |
| Record and send | [message-recording-and-sending.md](../../product/experiences/message-recording-and-sending.md) |
| Volume | [settings.md](../../product/experiences/settings.md) |
