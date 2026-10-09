# Family Link without removable storage: architecture assessment

> **Current direction (2026-10-07):** the user will buy supported cards and has
> authorized product-server implementation now. Removable-storage experiments
> remain paused. Server disk/chunk work is host-tested; see the
> [roadmap](product-no-storage-roadmap.md). The earlier planning-only boundary
> is superseded for this server implementation; device findings below remain
> outstanding unless explicitly marked implemented in their plans.


| Field | Value |
|---|---|
| **Doc kind** | `research/exploration` |
| **Owners / areas** | Product, firmware, server, interaction design |
| **Status** | `draft` — repository-grounded planning, no implementation |
| **Targets** | Current X02/v1 product, online operation with volatile outgoing media |
| **Last updated** | 2026-10-07 |
| **Supersedes / superseded by** | Temporary operating policy supersedes storage-first execution instructions; durable standards remain future requirements |
| **As-built** | None; proposed changes are not shipped |

## At a glance

**GO for the online Family Link experience without removable storage.** The accepted temporary limitation is loss of unaccepted outgoing audio/drawing after reset, power loss, or deliberate discard. This is **not** GO for the promised offline/reboot-safe durable long-message experience. Local durability is a deferred capability, not a prerequisite for the online product work indexed in [the roadmap](product-no-storage-roadmap.md).

| Assessment | Result | Status |
|---|---|---|
| [Storage dependency](#answers-to-the-seven-storage-questions) | Online core can proceed; durable guarantees cannot | done |
| [Current product audit](#repository-evidence-and-product-problems) | Existing defects identified separately from storage | done |
| [Target contract](#recommended-temporary-product-contract) | One volatile draft, verified acceptance, explicit outcomes | proposed |

## Authority and boundaries

The operator paused removable storage, has only a nominal 64 GB card, accepts temporary connectivity-related outgoing-message loss, and requested architecture/plans only. No more 64 GB SDXC experiments, no alternate local-filesystem qualification, no implementation sequence, and no automatic deployment follow from this planning pass. The preceding H38 run ended failed at its IO deadline; its controller verified full existing-image restoration/readback, partition/NVS/apps, eight zero recovery exit codes, and healthy original startup. That startup proof is not a device proof of the current repository's entire X02 journey. All attached-storage results remain unqualified.

This is an explicit temporary exception to decision 2 and the durable-outbox steps in [LONG-MESSAGE-ARCHITECTURE.md](../LONG-MESSAGE-ARCHITECTURE.md). Do not silently weaken [MESSAGE-PROTOCOL.md](../MESSAGE-PROTOCOL.md), call RAM writes durable, or report the storage/outbox experiments complete. The approved [v1 product](v1-product-spec.md) supports online async audio independently of those future guarantees. Drawing is out of that original merge specification but is already present in current X02 code; this roadmap makes that bounded existing feature honest rather than assuming full FLSK2 integration.

## Answers to the seven storage questions

1. **Can the intended experience operate without removable storage?** Yes: signed-in people can choose recipients, record in PSRAM, upload to the household server, receive a server-acceptance receipt, browse and play messages, and use settings/admin. Full online three-minute support needs the upload and playback improvements below; it is not already proven. No: offline capture guaranteed to survive reboot, unattended durable retry, and the complete durable long-message specification cannot operate without a qualified persistent local backend.
2. **What works normally?** Roster/PIN, online recipient selection, online audio and bounded current drawing capture, server-side one-to-one/broadcast delivery, inbox navigation, online playback, volume/appearance, and parent administration do not inherently require SD. Device NVS for small existing settings remains available. Existing software defects in these areas are still defects, not storage requirements. No broad offline playback cache is currently implemented.
3. **What becomes less reliable/unavailable?** No power/reset-safe drafts, no reliable offline recording queue, no retry after device reboot, no safely queued indicator, and no unlimited outgoing recordings while offline. Incoming media still needs the server; current code fetches every playback, including after prior playback. Only one unsent RAM draft is recommended, with a measured duration/memory limit.
4. **Exactly when can a message be lost?** See the failure matrix below. A client timeout alone does not prove loss: the server may have accepted the message and its reply may have been lost. Persistent client IDs plus server receipt lookup resolve that ambiguity while the RAM descriptor remains. After reboot, a volatile client cannot reconstruct that descriptor or promise recovery.
5. **What does the child experience?** Known-offline start is blocked with a short explanation; a valid recording ending during a connection failure gets an explicit failure/uncertain-delivery screen. Once plans 04/05 are implemented, the same recording can be retried while it remains in RAM, or discarded explicitly. Explain “Keep this box on to try again.” Never claim “saved,” “queued safely,” or “sent” without the corresponding capability/evidence. After reboot the child signs in again; the box cannot truthfully identify or recover a vanished draft from RAM alone. An adult-facing operating note states that limitation. If the server accepted it, it may still arrive even when the sender never saw confirmation.
6. **What choices would make SD difficult later?** UI directly calling filesystem/mount APIs; using recipient sequence numbers as global message IDs; changing IDs on retry; retaining references to mutable session/recipient arrays; freeing media on ambiguous acknowledgement; making a payload one mandatory contiguous pointer; conflating RAM retention with persistence; encoding paths in the wire protocol; and tying progress to a screen instead of a message operation. Plan 01 prevents these through a small controller/payload-reader/transport seam, explicit capability flags, and frozen operation context.
7. **What already fundamentally depends on local durability?** The offline provisional-ID → persisted chunks → reboot recovery → background retry lifecycle in LONG-MESSAGE-ARCHITECTURE and the durable outbox/long-message experiment plans does. H32 failed qualification and H38 never qualified; neither can be used as an invisible substitute. Streaming incoming playback, canonical server storage, online sending, receipt UX, and even an online chunk transport do not fundamentally depend on local persistence. Opus has an independent application-partition/resource decision; lack of SD is not its only prerequisite. This roadmap keeps PCM/current application geometry and defers codec expansion.

## Failure and loss matrix

| Boundary | Current repository behavior | Target temporary behavior |
|---|---|---|
| Known offline before start | Picker/start generally blocked using a connection flag | Block start without destroying selections; no pretend recording/queue |
| Mic/allocation error or immediate stop | Often zero-length capture or toast; generic failed send; capture prefix may be sent after read error | Distinguish empty, failed, cancelled, and valid capture; no invisible upload of incomplete capture |
| Wi-Fi/server lost during recording | Entire capture remains only in PSRAM until upload attempt | Finish only within measured RAM limit; keep one closed RAM draft for an explicit retry/discard decision |
| Failure before server acceptance | Recording has no durable owner and is logically abandoned/overwritten by subsequent capture | Sticky “Couldn't send”; retain the one RAM draft until accepted or explicitly discarded, unless reset/power/explicit privacy boundary loses it |
| Server commits, reply lost | Status-only client cannot resolve outcome; another send can duplicate | “Couldn't confirm delivery”; query/retry the same immutable client ID; never create another delivery |
| Subset of sequential recipients succeeds | Last status collapses partial delivery to one result | Interim per-recipient truth, then one atomic recipient-set commit with one stable logical ID |
| Box reset/power loss before acceptance | RAM descriptor/media gone | Still gone; no reboot-safe claim. Server may retain incomplete or already accepted media; only complete messages are visible |
| Discard, new capture overwrites, logout/privacy | Globals/session can change while worker continues | Deliberate lifetime policy, cleanup only after worker releases payload, no cross-user media or stale UI events |
| Server restart after apparent success | Media bytes may remain, but UserMailbox metadata resets and bootstrap rewrites the welcome slot | Accepted server messages/index identity survive process restart before calling this a coherent pilot; server loss is not covered by the local-storage waiver |

## Repository evidence and product problems

Inspection baseline: repository HEAD `eae71ab` (2026-10-07); product code was read, not flashed or behaviorally qualified in this planning pass. Symbol references are more stable than line numbers. Old feature/plan claims are not substitutes for source or device evidence.

| Finding | Evidence | Consequence / plan |
|---|---|---|
| Receipt already exists | [v1_record.c](../../firmware/v1/v1_record.c): `send_phase_t`, `paint_send`, `record_task_fn` | Four phases exist, but status-only success, 2s disappearance and the 25s watchdog can mislead; 01/02 |
| A 2xx status can override transport failure | `post_wav` returns status before testing the perform result | False “Sent” after incomplete response; 02 |
| Worker owns shared mutable identity/state | `s_session`, recipient arrays, worker `v1_state_apply`, shell mute/sign-out | Late completion can affect another session/recording; 01/14 |
| Partial multiple-recipient delivery | `record_task_fn` sequential `post_wav` loop; web twin uses same pattern | Whole-message failed/sent labels omit partial truth; 02/04 |
| Server acceptance is not restart-safe today | [user_mailbox.py](../../demos/server/_shared/user_mailbox.py): constructor, `_append_message`, `bootstrap_mailbox` | Inboxes/session/profile/sequence metadata are in memory; bootstrap seeds seq 1 rather than recovering it; 03 |
| Same URI has different contracts | [v1_product/server.py](../../demos/server/v1_product/server.py), [combined/server.py](../../demos/server/combined/server.py), [h34/server.py](../../demos/server/h34_message_store/server.py) | Product multipart POST, older combined POST, and future JSON chunk-create are not interchangeable; 00/04 |
| Duration-dependent RAM twice | `pcm_buffer_ready` reserves about 5.76 MB; `post_wav` copies a second complete multipart body | A long recording can fit but fail on Send; 06 |
| Playback truncation | [v1_api.c](../../firmware/v1/v1_api.c) reads only cap−1 without requiring full response; [v1_carousel.c](../../firmware/v1/v1_carousel.c) plays a 320 KiB prefix as a blob | About 10.24 seconds of PCM capacity despite a 180s recording setting; 11/12 |
| Playback/read feedback wrong on errors | `playback_task` treats a write-break like end; `mark_read` updates local state after ignored server response; WS refresh stops playback | Silent failures, premature read state, interrupted listening; 12/13 |
| Drawing lifecycle resets/clamps silently | `paint_record` clears points/canvas and resets clock on repaint; [v1_sketch.c](../../firmware/common/v1_sketch.c) clamps to 30,000 ms / 2,048 points | Lost drawing on repaint, late strokes replay at 30s, pack error can omit sketch; 08/09 |
| Recording stop reasons differ from copy | `record_take`: near-zero branch at 0.5s, silence threshold, trim wait plus memmove; repaint-derived clock | Unexpected stop and unclear failed/empty capture; 08 |
| Connectivity blocks UI and confidence is stale | Picker painting performs HTTP; shell Wi-Fi retry blocks for up to 25s; WS failures are ignored during RECORD/SEND | Frozen controls and stale “online”; 10 |
| Invalid inbox can erase good view | `v1_api_parse_inbox` sets count to zero before parsing; oldest-first truncated 16-item window | Empty inbox on truncated JSON, newer messages can vanish; 13 |
| Appearance/admin reliability gaps | profile/read/session state volatile; save replies largely ignored; product `server.py` uses `time.time()` without importing `time` | Separate profile/admin feedback and persistence work; 15 |
| Browser twin is not recording proof | [box.js](../../demos/server/v1_product/web/box.js) `tinyWavBlob`, `runOutboundSend`, exception `String(e)` toast | Generated-audio preview, different failures and raw exception copy; 16 |
| Documents conflict | v1 spec says picker auto-start/10s timeout/1min relock/audio-only; current code selects then Record/no timeout/10min relock/drawing | Each scoped plan updates only its corresponding contract; readiness must not be inferred from old prose |

## Recommended temporary product contract

- One outgoing message operation per signed-in user/device at a time; freeze sender, recipient IDs and effective Everyone set, client UUID, media type/limits and session generation.
- Only known-online starts for now. A later drop can leave one RAM draft. No local persistent outbox, no unattended retry loop, no new recording while that draft is unresolved. Retry is explicitly user initiated after idempotent server support.
- Lifecycle: `inbox → choose → preparing → recording → finishing → sending → sent → Done → same inbox`. Recoverable failure: `sending → failed/uncertain → Retry (same message) or Discard`. A discard cannot recall server-accepted media.
- “Sent” means complete validated server acceptance for the exact message/media and recipient set, not that recipients listened and not that a progress bar reached 100%. User-facing copy remains simple; IDs/status codes belong in private diagnostic evidence.
- Keep PCM/current FLSK1 compatibility and existing app partition. Raise neither recording limit nor memory promise from paper arithmetic. Stream uploads from the existing RAM payload without a second complete copy, and stream playback through bounded RAM. No SD requirement for either improvement.
- Preserve bounded existing drawings at most 30s/2,048 points with explicit limit feedback; full-message FLSK2 and Opus integration remain distinct deferred work. Repaint must never initialize message content.
- Use a controller with typed, generation-bound events; a payload store with truthful capabilities (`volatile`, no reboot recovery); and a transport/receipt adapter. UI consumes message status, not hardware storage objects.
- Archive completed media on the server. Acknowledged server message loss on normal process restart is a separate product defect to repair, not an accepted consequence of missing SD.

## External documentation, inference and recommendations

- **Manufacturer evidence:** [SENSOR specification](https://docs.espressif.com/projects/esp-board-manager/en/latest/references/boards/esp32_s3_box_3.html) advertises up to 32 GB. The operator stopped the 64 GB work; no more compatibility research/experiments are needed.
- **Manufacturer API evidence:** [ESP-IDF 5.4.2 HTTP client](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/api-reference/protocols/esp_http_client.html) distinguishes operation result from HTTP status, offers streaming request/response operations, and returns actual write lengths. Therefore loops must handle short writes and complete bounded responses. This enables plan 06 without a second payload-sized allocation.
- **Manufacturer API evidence:** [ESP-IDF heap allocation](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/api-reference/system/mem_alloc.html) provides capability/largest-block inspection. [NVS](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/storage/nvs_flash.html) is not a reason to turn message media into an unqualified hidden flash outbox. Existing small configuration use is separate.
- **Repository proof:** H34 has a qualified host-only PCM message-store island, not current product integration, sketch support, deployed Windows-host qualification or power-loss proof. Reuse reviewed primitives with tests; do not import its whole app/global data root or claim X02 gained its guarantees automatically.
- **Inference:** removing the duplicate upload allocation plausibly enables the existing 180s PCM budget on the configured PSRAM, but fragmentation/other consumers may still prevent it. Device memory and full playback tests decide; inability to meet 180s is a stop/explicit-cap decision, not a reason to resume SD experiments.
- **Recommendations:** explicit Done receipt, one retained RAM draft, manual same-ID retry, server committed acceptance, bounded playback, privacy ownership rules. These are proposed product improvements, not current observed behavior.

## What remains deferred or requires a genuine decision

No product-direction decision is needed to begin the baseline and lifecycle plans. The temporary loss policy is already authorized. Real-world verification requires a configured product host and eventually a person/approved UI harness for physical interactions; pending device evidence must stay pending. Production server-data migration requires a lossless inventory/export, and an owner decision if live metadata cannot be recovered. Server-filesystem durability must be qualified on the actual deployment host before stronger power-loss language. PIN-verifier persistence beyond existing security conventions is a separate credential-policy gate in plan 15. A smaller card, OTA layout, Opus or full-message FLSK2 is not a dependency of the current online plans.
