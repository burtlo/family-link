# Roadmap: Online product while removable storage is paused

| Field | Value |
|---|---|
| **Doc kind** | `version-roadmap` |
| **Status** | `active` |
| **Owners / areas** | Product / server / firmware |
| **Targets** | Product server on the user's personal computer; online X02 |
| **Last updated** | 2026-10-07 |
| **Supersedes** | Storage-first execution while supported cards are unavailable |
| **As-built** | [Server host evidence](../evidence/product-no-storage/03-server-recovery/README.md) |

## At a glance

**GO for online development without removable storage.** Temporary outgoing
messages can be lost on reset/power loss before confirmed server acceptance.
Accepted messages belong in the durable server archive. Storage does not make a
25-second frozen screen necessary or acceptable; asynchronous device/UI repair
is still required.

The user authorized server implementation after the earlier planning-only
request. Supported cards will be purchased; return to device storage later.

| Phase | Outcome | Status |
|---|---|---|
| [Server](#server) | Disk archive and PCM chunk API | Host-tested; deployment pending |
| [Device work](#device-work) | Responsive, truthful send/retry experience | Draft plans; not implemented |
| [Deferred storage](#deferred-storage) | Durable device outbox | Deferred |

## Server

[Plan 03: accepted-message recovery](product-03-server-accepted-message-recovery.md)
is implemented in the product server. Complete WAV/FLSK1 media and metadata are
committed on disk; SQLite stores mutable read/position/profile state. PCM chunk
uploads and completion can resume after restart. Existing multipart devices
remain compatible. Broadcast media is shared; recipient references commit as a
set. Read responses stream files. Same client/chunk identities support retries
without new inbox entries.

Use the [personal-computer run guide](../../demos/server/v1_product/README.md).
Data defaults to the user's application-data folder, with configurable local
disk placement. It is not canonical Python memory or a source-checkout-only path.

**Deployment gates:** identify the user's target computer/data location; qualify
that filesystem. If replacing an existing server, export live metadata before
its restart and verify migration into a separate root. No live server/device was
changed by this work. Windows and power-loss behavior are not qualified by Mac
host tests. Old media is preserved; no implicit import/reseed is permitted.

This is a PCM server subset, not completed Opus/FLSK2/device protocol integration.
Admin tokens and PIN-reset overrides remain volatile. Archive APIs paginate
older messages; current firmware does not yet expose older-page navigation.

## Device work

These are independent draft assignments, not completed changes. A fresh agent
should start with the baseline gate before making firmware changes:

1. [00 — Verify device/product baseline](product-00-baseline-gate.md).
2. [01 — Own one volatile outgoing message](product-01-volatile-message-lifecycle.md).
3. [02 — Truthful, responsive send receipt](product-02-truthful-send-receipt.md).
4. [04 — Delivery identity/receipt integration](product-04-idempotent-delivery-receipts.md).
   Some server identity/status foundations now exist, but the exact multi-target
   receipt contract and device integration in this plan are not complete.
5. [05 — Manual RAM retry and explicit discard](product-05-ram-retry-and-discard.md).

Further identified work: remove the duplicate full upload buffer; repair picker
selection/repaint, recording and drawing timers/limits; move all connectivity
work away from UI painting; implement bounded full-length playback and archive
navigation; reconcile mute/session cleanup. The
[assessment](product-without-removable-storage-assessment.md) records current
code evidence and exact loss boundaries. These findings remain outstanding;
server completion must not be reported as a repaired child-device journey.

## Deferred storage

**DEFERRED — DO NOT IMPLEMENT UNTIL SUPPORTED STORAGE IS AVAILABLE AND QUALIFIED.**

No more 64 GB experiments. A card arriving is a prerequisite, not a passed
qualification. Revisit local durable chunks, acknowledgement-driven reclamation,
retry/reboot recovery and owner/privacy handling through the existing
[architecture](../LONG-MESSAGE-ARCHITECTURE.md),
[protocol](../MESSAGE-PROTOCOL.md), and paused
[experiment plan](long-message-experiments.md). Do not use NVS or an unqualified
internal filesystem as a hidden audio outbox.
