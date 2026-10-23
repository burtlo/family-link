# V2 core journey

Status: **preview — spec incomplete** (published 2026-10-22).

This bundle maps the core V2 journey: login, inbox and playback, record/send, and Volume. Profile editing and the settings hub are out of scope. Experience specifications under `docs/product/experiences/` are the behavior source. There is no `spec.md` or `retro.md` here.

The map does not authorize firmware work and does not approve wire. It becomes ready for orchestration only after that preview is accepted and **P1** records an accepted wire revision.

## Artifacts

| File | Purpose |
|------|---------|
| [implementation.md](implementation.md) | **Canonical** — deltas, work packages (P1–P10), verifier hooks, build order |

## Normative sources

| Experience | Spec |
|------------|------|
| Signed-out login | [endpoint-login.md](../../product/experiences/endpoint-login.md) |
| Inbox and playback | [message-carousel-and-playback.md](../../product/experiences/message-carousel-and-playback.md) |
| Sketch geometry | [sketch-record-and-playback.md](../../product/experiences/sketch-record-and-playback.md) |
| Record and send | [message-recording-and-sending.md](../../product/experiences/message-recording-and-sending.md) |
| Volume | [settings.md](../../product/experiences/settings.md) |
