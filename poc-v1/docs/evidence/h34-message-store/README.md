# h34 message_store qualification

This evidence qualifies the host-only PCM filesystem subset of the durable message protocol. The authoritative contracts are [MESSAGE-PROTOCOL.md](../../MESSAGE-PROTOCOL.md) and [SERVER-MESSAGE-STORAGE.md](../../SERVER-MESSAGE-STORAGE.md); the acceptance gates are in [h34-message-store-corrections.md](../../plans/h34-message-store-corrections.md). The machine-readable [qualification.json](qualification.json) and human-readable [qualification.log](qualification.log) are the retained result.

## Reproduce

From a clean checkout at the repository root, install the server dependencies, then run the full qualification:

```bash
make install-server
set -o pipefail
make qualify-message-store 2>&1 | tee docs/evidence/h34-message-store/qualification.log
```

`make qualify-message-store` writes `docs/evidence/h34-message-store/qualification.json`. It creates an isolated temporary `FAMILY_LINK_ROOT`, temporary device registry, and local server on an ephemeral loopback port. It generates its PCM payload from `integer-lcg-v1`; no preexisting message store, device credential, family recording, or external network service is needed. The quick interactive smoke demonstration remains `make demo-message-store`; it is separate from this qualification.

## Fixture and observed result

The retained JSON reports run `aa1ea0ac-51d9-4aca-994e-be0911056db5` at tested source revision `d0f70c42781ecb433dd7d624ce016cf87b1db955`; the matching log ends in **RESULT PASS**. All 21 named checks and all 50 injected process-crash boundaries passed: 10 chunk, 20 completion, 11 cull, and 9 restore. The fault runs restart the server and check canonical directory count, complete marker, inbox multiplicity, hashes, and route visibility.

| Fixture property | Value |
|---|---:|
| Duration | 180,000 ms |
| PCM format | 16,000 Hz, mono, signed 16-bit little-endian |
| Chunk interval and count | 2,000 ms; 90 chunks |
| Bytes per chunk | 64,000 |
| Raw PCM bytes | 5,760,000 |
| Canonical WAV bytes | 5,760,044 |
| PCM SHA-256 | `8d1bbf8aed233815eeae3a3ba2e844bf3a6fc42ee4b107a7a21f5e5991d8e74a` |
| WAV SHA-256 | `798038d711184e4a46503da29f5c73082938c38d864a96351a0589034a821f4a` |

The completed recipient inbox contains one reference. Full media GET returns 5,760,044 bytes with the WAV hash above. The JSON records four deterministic Range responses: bytes `0–63` (`896784ab7a22fd30f6995128bd34ccb4a12bbe0e732fad0cc38915300b22e528`), `2880022–2880149` (`807bb8c5e0348acb8d598e7abb761d20e362e9fac39fb675e3fd757cfe36fa2f`), `5759916–5760043` (`1ae631328b7cfe09e7d8e141d5496501ca242cfd4e7380c9f9f96a9afc52bdd8`), and the full range (the WAV hash). The checks also cover reordered upload, duplicate/conflicting chunks, authorization, limits, low-disk refusal and recovery, restart, exact cull preview, trash, restore, malformed state quarantine, and replay/stale-selection rejection. Individual outcomes and boundary names are in the JSON.

## Memory and durability scope

The recorded host was macOS 27.0.1 on arm64 with CPython 3.14.7. The local server environment used FastAPI 0.141.1, Starlette 1.6.0, Uvicorn 0.52.4, and HTTPX 0.28.1. The harness measured server-side Python allocation per ASGI request using `tracemalloc.reset_peak`, subtracting the live allocation baseline; streamed responses are included. The 180-second upload peak was 341,852 bytes, finalization 530,977 bytes, small Range 20,929 bytes, and full Range 265,013 bytes. The largest peak was 517,599 bytes under the predeclared 1,048,576-byte gate. The short 2-second comparison was 345,354 bytes for upload, 417,623 bytes for finalization, and 283,380 bytes for full Range. The server read blocks were at most 64,000 bytes per request and 65,536 bytes per response. The measured server RSS peak was 66,846,720 bytes; RSS is reported separately from the allocation gate.

The fault campaign injects process termination and starts a fresh process against the same isolated store. It supports a **process-crash durability** claim on this host. It does not establish switched-power-loss durability or prove the filesystem and disk honor every flush under sudden power loss.

## Remaining protocol work

This host proof covers PCM/WAV canonical storage, upload, completion, inbox lookup, Range playback, and administrative cull/restore. Opus/Ogg, sketch chunks, upload abort, notification delivery, device outbox, BOX-3 playback, and X02 product integration remain separate work in [long-message-experiments.md](../../plans/long-message-experiments.md). The production partition choice and device storage qualification are also outside this run.
