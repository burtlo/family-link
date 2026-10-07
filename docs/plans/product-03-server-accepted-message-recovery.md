# Plan: Preserve accepted messages on server restart

| Field | Value |
|---|---|
| **Doc kind** | `feature-plan` |
| **Owners / areas** | Product, firmware/server as scoped below |
| **Status** | `active` — implemented and host-tested; personal-PC deployment/migration pending |
| **Targets** | X02/v1 online product; removable storage unavailable |
| **Last updated** | 2026-10-07 |
| **Supersedes / superseded by** | Corresponding behavior in older demo plans only; durable guarantees remain deferred |
| **As-built** | [Host implementation evidence](../evidence/product-no-storage/03-server-recovery/README.md) |
| **Depends on** | Product host identified by source/API tests; 00 device gate applies before firmware work |
| **Implementation readiness** | Server implementation complete locally; verify target computer before deployment |

## At a glance

Messages the server accepts remain discoverable with the same recipients and sequence identities after normal server restart.

| Phase | Outcome | Status |
|---|---|---|
| [Implementation and proof](#goal) | Preserve accepted messages on server restart | host proof complete; deployment pending |

The user authorized server implementation on 2026-10-07, superseding the earlier planning-only boundary for this server scope. This is one bounded assignment. Read [the operating assessment](product-without-removable-storage-assessment.md) for policy context, but all required outcomes are stated here. No SD experiments, device filesystem/partition changes, Opus integration or autonomous sequence is authorized. 

## Goal

Messages the server accepts remain discoverable with the same recipients and sequence identities after normal server restart.

## Why

The local RAM-loss exception applies before acceptance. It must not excuse a Sent message disappearing from the canonical household archive.

## Current behavior

`UserMailbox` writes direct/shared WAV and sketch bytes but stores Message, inbox, next_seq and session/profile metadata only in Python memory. `bootstrap_mailbox` creates a new mailbox and seeds seq 1 rather than rebuilding history. Restart can lose inbox references and overwrite a prior welcome slot. H34 provides an independent, qualified PCM store, not a drop-in product app or sketch implementation.

## Target behavior

Give each accepted logical message a canonical immutable manifest/media commit under the product data root. A manifest contains stable message ID, sender/label, exact recipients with per-user seq, server timestamp, system flag, duration, audio and optional FLSK1 sizes/hashes and format. For broadcast use one media copy with several references. Stage complete assets, validate them, commit atomically, then publish in-memory views/return success. Rebuild views and sequence high-water marks from committed manifests at startup; ignore/quarantine incomplete/corrupt staging without fabricating inbox entries. Bootstrap adds a welcome only when an authoritative manifest inventory proves it is absent. Preserve existing API response shapes and seq blob/sketch lookup via an adapter.

## Scope

Implement one narrow product archive module and adapt post_audio/welcome/read lookup/startup. Reuse reviewed H34 durable primitives if appropriate, without importing H34 app globals/auth/data paths. Record filesystem commit support and qualify process-crash/restart on the intended host. Separate server storage failure from accepted commit. No success before media plus manifest commit. Preserve existing byte-oriented compatibility methods as adapters; a later stream path can replace their memory materialization.

## Non-goals

No device outbox, Opus/FLSK2 migration, automatic culling, changing auth, or deployment over existing family data. Server PCM chunk uploads were additionally authorized and implemented; firmware migration remains pending. Profile/PIN/state durability is separately scoped in 15; message manifests remain immutable.

## Relevant implementation surfaces

`demos/server/_shared/user_mailbox.py` (`_append_message`, `post_audio`, `bootstrap_mailbox`, read methods); `demos/server/v1_product/server.py` (post_message/welcome); proposed product archive module; `demos/server/h34_message_store/durable.py` as reference; `docs/SERVER-MESSAGE-STORAGE.md`.

## State and transitions

`validated request → staging → committed manifest/media → visible inbox references → response`. Restart reconstructs only committed messages. Interrupted publication is repaired from the commit, not acknowledged as another message. Stable seq values are never reused for existing identities.

## Edge cases

Crash after media write/before manifest; commit before in-memory publish; broadcast crash between recipients; corrupt manifest; missing sketch; full disk; duplicate welcome seed; orphan legacy media; two concurrent sends; Windows directory-sync limitations.

## Acceptance criteria

- Accepted direct, broadcast and audio+drawing messages retain identical IDs, seq, recipients and hashes after restart.
- No incomplete request becomes visible, and no startup overwrites an existing welcome/media file.
- Disk failure before commit returns non-success; committed data remains recoverable after later notification failure.
- Tests document actual host sync guarantees; no invented power-loss durability.
- Legacy API consumers still resolve media and the same inbox fields.

## Verification

Automated host tests in a new temp root: create/restart/compare, injected process boundaries, broadcast and sketch hashes, ENOSPC and corrupt-manifest isolation, sequence reconstruction and welcome idempotence. Rerun existing v1 client against disposable test identities. Before deployment, losslessly export live in-memory metadata while its old process still runs; copy/verify data and perform a staged import rehearsal. Never restart the real old host simply to test recovery.

## Evidence to leave behind

`docs/evidence/product-no-storage/03-server-recovery/`: manifest schema, adapter mapping, fault boundary results, before/after hashes/IDs with synthetic media, actual OS/filesystem durability report and separate deployment/migration readiness. Raw family inventory/export stays private.

## Stop conditions

Stop before live server restart/migration if current in-memory metadata cannot be exported or reconstructed without guessing sender/time/seq. Preserve orphan bytes and ask the owner about handling; no wipe/reseed. Stop on cross-filesystem atomicity or filesystem limitations requiring a new durability promise. Local SD availability is not a blocker here.

## As-built update — 2026-10-07

The product server now uses a configurable archive outside the checkout by
default, immutable `family-product-manifest/1` media commits, restart index
recovery, and SQLite read/position/profile state. It supports existing multipart
WAV/FLSK1, sender-owned client-ID receipt lookup and a resumable PCM subset of
`family-message/1`. No complete media is loaded into RAM by its HTTP upload/read
paths. Bounded file helpers are adapted without importing the H34 app.

21 synthetic host tests pass, including actual process termination at chunk and
commit boundaries and three-minute media processing. See the
[run guide](../../demos/server/v1_product/README.md) and
[evidence](../evidence/product-no-storage/03-server-recovery/README.md).
Corrupt archives fail startup while preserving files. Legacy data is guarded;
no live metadata migration or personal-PC deployment has occurred.

Windows filesystem qualification, migration of an existing live server, and
power-loss qualification remain open. Admin tokens and PIN-reset overrides
remain volatile. Current firmware still uses multipart, does not paginate older
inbox entries, and has not received the UI/send/playback repairs in other plans.
