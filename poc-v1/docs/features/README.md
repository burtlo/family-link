# V1 product features (as-built index)

Feature records describe **observable behavior backed by code** in `demos/server/v1_product/`, `firmware/v1/`, and related web assets. Product intent and future scope remain in [`plans/v1-product-spec.md`](../plans/v1-product-spec.md).

**Status legend**

| Status | Meaning |
|--------|---------|
| **Verified** | Automated tests in `demos/server/v1_product/tests/` (or named script) prove core behavior |
| **Implemented** | Shipping code path, no dedicated automated test or device CI |
| **Partial** | Subset of spec or split client/server maturity |
| **Planned** | Approved spec only |
| **Experimental** | Server or lab path not used by production firmware flow |
| **Superseded** | Replaced by another feature or host |

Architecture: [`architecture/overview.md`](../architecture/overview.md).

## Feature table

| Feature | Doc | Primary status | Notes |
|---------|-----|----------------|-------|
| PIN auth and session | [pin-auth-and-session.md](pin-auth-and-session.md) | Implemented / Partial | Login/PIN-reset lack unit tests; firmware manual |
| Carousel and recipients | [carousel-and-recipients.md](carousel-and-recipients.md) | Partial | Inbox paging verified; device page size 8 only |
| Async audio record and send | [async-audio-record-send.md](async-audio-record-send.md) | Verified / Partial | Multipart verified; discard-on-fail on device |
| Message playback and sync | [message-playback-and-sync.md](message-playback-and-sync.md) | Verified / Implemented | Range + read/position restart tests |
| Web admin parity | [web-admin-parity.md](web-admin-parity.md) | Verified / Implemented | Tests: login, send; PIN reset manual or client smoke |
| Server disk archive | [server-disk-archive.md](server-disk-archive.md) | Verified | 21 tests, crash/restart |
| PCM chunk upload | [pcm-chunk-upload.md](pcm-chunk-upload.md) | Verified / Experimental | Host-only until firmware adopts |
| Endpoint connectivity and push | [endpoint-connectivity.md](endpoint-connectivity.md) | Partial | WS hello tested; ops contract draft |
| Settings, sleep, profile | [settings-sleep-profile.md](settings-sleep-profile.md) | Implemented / Partial | Profile API verified |

## Count by status (features above)

| Status | Count |
|--------|------:|
| Verified (primary or co-primary) | 6 |
| Implemented (primary) | 2 |
| Partial (primary) | 3 |
| Experimental | 1 |
| Planned | 0 |
| Superseded | 0 |

*Several rows use dual labels (e.g. Verified on server + Implemented on firmware); table counts the **primary** column in the feature doc header.*

## Superseded hosts (reference only)

| Item | Replaced by |
|------|-------------|
| `demos/server/combined/server` for product | `demos/server/v1_product/server` |
| x01 glue demos | x02 + `firmware/v1/` |

## Authoring

New records: use sections **Purpose, UX, Behavior, Implementation, Constraints, Verification, Remaining Work**. Template notes in [`_template.md`](_template.md) describe an alternate layout for non-v1 work — prefer the v1 sections above for product features.
