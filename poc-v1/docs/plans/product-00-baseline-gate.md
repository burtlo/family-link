# Plan: Verify the actual product baseline

| Field | Value |
|---|---|
| **Doc kind** | `feature-plan` |
| **Owners / areas** | Product, firmware/server as scoped below |
| **Status** | `draft` — not implemented |
| **Targets** | X02/v1 online product; removable storage unavailable |
| **Last updated** | 2026-10-07 |
| **Supersedes / superseded by** | Corresponding behavior in older demo plans only; durable guarantees remain deferred |
| **As-built** | None — leave an evidence link when implemented |
| **Depends on** | None |
| **Implementation readiness** | Ready now; device evidence may require an operator |

## At a glance

Identify the running product and leave a reproducible baseline and PIN gate before feature edits.

| Phase | Outcome | Status |
|---|---|---|
| [Implementation and proof](#goal) | Verify the actual product baseline | todo |

This is one bounded assignment. Read [the operating assessment](product-without-removable-storage-assessment.md) for policy context, but all required outcomes are stated here. No SD experiments, device filesystem/partition changes, Opus integration or autonomous sequence is authorized. 

## Goal

Identify the running product and leave a reproducible baseline and PIN gate before feature edits.

## Why

A demo build or restored app descriptor does not prove that a child can sign in and complete the current journey.

## Current behavior

`firmware/demos/x02_product_shell.c` is a shim; `firmware/v1/x02_main.c` is the product entry. The current device was restored after H38, but its product UI/source correspondence has not been verified in this planning pass. `v1-isolation-remaining.md` lists the INT-014 PIN device gate as open. The product server is `demos/server/v1_product/server.py`; combined and H34 expose similarly named but incompatible routes.

## Target behavior

Identify firmware revision/image/demonstration, actual host application and protocol, and the current approved settings. Observe good PIN, wrong PIN, unavailable server during checking, short generated-audio send, actual receipt, and short playback. Report known defects as baseline, not as a passed journey. No product feature is implemented here.

## Scope

Inventory existing manifests/logs first. Use a disposable host fixture root and configured test identities for API checks. On device, prefer observing the restored product; if a product flash is necessary follow repository device-verification procedures and permitted app bounds. Record which tests need a human or approved touch harness; never fabricate taps. Produce one dated baseline report.

## Non-goals

No storage/card operations, new partition, server migration, UI redesign, or mixed auth/record fixes. No normal family messages or production data in tests.

## Relevant implementation surfaces

`firmware/v1/x02_main.c`, `v1_auth.c`, `v1_record.c`, `v1_carousel.c`; `scripts/flash.py`; `demos/server/v1_product/{server,client}.py`; `demos/server/combined/server.py`; `docs/plans/v1-isolation-remaining.md`; `.cursor/rules/v1-auth-scope-freeze.mdc` and `.cursor/skills/device-test-after-flash/SKILL.md`.

## State and transitions

Baseline login: connecting → roster → PIN → checking → carousel, or wrong PIN / connecting. Capture baseline outbound picker → record → ST_SEND phases → carousel without claiming all outcomes correct.

## Edge cases

Wrong server on the expected port; old device binary; no touch operator; missing test credentials; failed PIN; known empty-recording or long-playback defects.

## Acceptance criteria

- Running firmware/host are identified with non-secret reproducible provenance, or explicitly unknown.
- Correct/wrong PIN and transport-failure outcome have real device evidence before a device gate is marked passed.
- Each known product defect is listed separately from missing hardware.
- Pending device work remains pending; host success never becomes device success.

## Verification

Host: use the v1 smoke client against an isolated root, inspecting its test-token assumptions first. Existing `make check-v1-parity` only proves selected timing parity. Device/manual: use the repo PIN checklist and generated or consenting test audio; record screen plus private serial provenance. No long storage experiments.

## Evidence to leave behind

`docs/evidence/product-no-storage/00-baseline/README.md`: revision/service matrix, commands/exit codes, observed screens, known failures, pending manual gates and next bounded plan. Keep credentials, raw household payloads and detailed private logs out of Git.

## Stop conditions

If PIN/connection fails, stop feature work and hand off the existing scoped auth plan; do not repair carousel/recording in this assignment. Stop on missing image provenance or destructive flash/server operations. Absence of a touch operator is a pending verification gate, not permission to invent a pass.
