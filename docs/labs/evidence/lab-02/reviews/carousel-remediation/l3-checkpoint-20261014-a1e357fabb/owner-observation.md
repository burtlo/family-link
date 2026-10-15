# LAB-02 L3 owner observation — 2026-10-14

Run: `20261014-a1e357fabb`

The canonical managed HTTP manual run completed with `workflow_status=PASS`
and `experiment_verdict=UNMEASURED`. The owner observed the device during the
run:

| Observation | Owner result | Evidence disposition |
|---|---|---|
| Persistent loading overlay or missing glyph | Not observed | L3 visual observation PASS for this run |
| Blank or stale cards during free traversal | Not observed | L3 visual observation PASS for this run |
| Rapid reversal before snap completion | Serviceable | Qualitative L3 observation; not a numeric timing or identity-gate PASS |
| Tap-to-retry after an initial-load failure | Not tested | L3 gap remains |

The run manifest recorded a fresh boot marker and zero dropped capture events,
but no scenario-specific verification levels. The run therefore does not close
the C5 metrics/resource gates or the overall LAB-02 experiment verdict.

Retry affordances for a future targeted run are:

- initial-load failure: tap the `LAB-02 inbox unavailable` status label;
- failed focused page: tap the card showing `Message unavailable`.

## Targeted retry follow-up

Run: `20261014-recycle-capture-e66eb7`

The owner tested the current fixed build `5e185f4a6c82733df59817cf` with the
server unavailable. Tapping the initial-load affordance showed
`inbox retrying...`, then returned to `inbox unavailable - tap to retry`.
The serial trace recorded `owner_retry_initial`, a second timeout, and a
terminal failure without an `esp_netif` assertion or `RTC_SW_CPU_RST`.

| Finding | Owner result | Evidence disposition |
|---|---|---|
| Retry acknowledgement copy | Observed | L3 PASS |
| Retry terminal failure copy | Observed | L3 PASS |
| Retry after initial HTTP timeout reboots device | Not observed | LAB02-F002 L3 PASS |

## Explicit page-retry follow-up

Run: `20261014-recycle-capture-f8b35a`

After flashing the explicit page-retry remediation, the owner reported that
the device remained stable in use, that unavailable cards no longer visibly
wobbled while the server was offline, that tapping an unavailable card gave a
visible retry/loading transition, and that cards populated again after the
server returned. The operator capture completed with `workflow_status=PASS`
and `experiment_verdict=UNMEASURED`.

The 45-second serial capture recorded `LAB02_READY` for build
`70f676ccb61c5df6e364d1f1`, `LAB02_HTTP_READY revision=1 items=64`, 16 fetch
admissions, 17 retained fetch deliveries, 17 terminal fetches, and 9
explicit page retries. It recorded no stack overflow, Guru/assert, reboot,
or trace-loss marker.

| Finding | Owner result | Evidence disposition |
|---|---|---|
| Failed page remains visually stable while offline | Observed | L3 PASS |
| Tapping failed card gives visible retry/loading feedback | Observed | L3 PASS |
| Server recovery repopulates the failed card | Observed | L3 PASS |
| Device remains stable during retry and recovery | Observed | L3 PASS |

## Resource/timing capture follow-up

Runs: `20261015-e38423f175` and `20261015-phaseb-capture-23236b`

The owner reported that the current build feels good in use: most of the card
graphics tearing seen in v1 is gone, and touch input feels very responsive.
Snapping feels somewhat slow. During deceleration, the card appears to slow to
a halt and then correct its position, rather than easing directly into the
final position. The desired snap behavior is not yet decided; this remains an
open UX observation for later review, not a confirmed defect.

| Finding | Owner result | Evidence disposition |
|---|---|---|
| Card graphics tearing versus v1 | Materially improved; most tearing gone | Qualitative L3 observation |
| Touch responsiveness | Feels very responsive | Qualitative L3 observation |
| Snap settling behavior | Feels slow with a late positional correction | Open UX observation; numeric snap timing captured separately |
