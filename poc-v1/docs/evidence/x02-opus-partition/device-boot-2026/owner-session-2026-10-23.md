# Owner session — 2125 geometry + live x02 / Opus probe (2026-10-23)

Night desk session after formal plan **E1–E4** closed. Extends [`summary.md`](summary.md); does not replace exercise verdicts.

## Headline

**The 2.125 MiB single-factory layout is viable on BOX-3 for both full x02 and the Opus-linked size probe**, with the **real v1 product shell** (hangout, carousel, record) on **`PARTITION_PROFILE=x02_2125`**. Owner confirmed **end-to-end “it works”** on **x02-opus-probe** after auth fix.

## What is device-proven (tonight + E1–E4)

| Claim | Evidence |
|-------|----------|
| **x02 (~1.50 MiB) boots and runs product UI on `factory` `0x220000`** | E2, E4 smoke, owner record session |
| **x02-opus-probe (~1.68 MiB) boots and runs the same UI on same geometry** | E3; owner re-flash 2026-10-23 evening |
| **~25% app slot headroom** on probe @ 2125 | Build: `0x199900` bytes in `0x220000` slot |
| **Record + UI stability** on 2125 (no reboot through record/send UX) | Private serial `~/family-link-private/x02-2125-owner-record-20261023/` |
| **Mandatory restore discipline** (16 MiB backup/readback) | [`restore-proof.txt`](restore-proof.txt) epochs E2–E4 |

## What is *not* proven

| Claim | Status |
|-------|--------|
| **Opus on the wire** (small encoded uploads) | **No** — probe only **links** `fl_opus` for image size; `v1_record` still **`post_wav`** (PCM). |
| **Opus encode during record** in x02 shell | **No** — use island **`h31`** + `h31_opus_messages` server for that pipeline. |
| **v1 send UX is trustworthy** | **Weak** — see upload timing below. |
| **Production `sdkconfig` switched to 2125** | **No** — owner/engineering decision; host + device evidence only. |
| **LAB-15 PCM envelope (~5.76 MiB)** | **Out of scope** — different question. |
| **Long soak / many messages on outbox @ `0x230000`** | **Not exercised** |

## x02 vs x02-opus-probe (why UI looks the same)

Both demos use **`firmware/v1/x02_main.c`** and the same screens. **Probe** adds `fl_opus` + `fl_opus_size_probe_force_link` so the binary matches “x02 + Opus dependency” size; **no separate Opus demo UI**. Island demos **h30** / **h31** are different apps (local Opus vs chunk upload).

## v1 client/server — upload and “connecting” (learned tonight)

### Hangout “connecting” / 401

- **Server host in secrets was already correct** (`DEMO_SERVER_HOST` on laptop LAN, port **8080**).
- **x02-opus-probe** was missing from `V1_PRODUCT_DEMOS` in `scripts/flash.py`, so `WHO=mazi` stamped **name** but left bearer **`change-me-a`** → **`GET /v1/hangout` 401** → endless connecting.
- **Fix:** add `x02_opus_size_probe` and `x02_opus_dep_only` to `V1_PRODUCT_DEMOS`; align `secrets.sunset.h` v1 tokens (`change-me-mazi` / `change-me-arlo`); reflash with `WHO=mazi`.

### Record / send (WAV, not Opus)

- **~10 s clip ≈ 320 KiB** WAV multipart — not “tiny” because **Opus is not in the send path**.
- **Device stayed stable** through record and Sending UI; **receiver empty** when upload failed or addressed wrong user.
- **Server:** some attempts **`ClientDisconnect`** mid-multipart; at least one **`POST /v1/messages` 200 OK** from device LAN IP.
- **Serial timeline (owner session):** UI **`send receipt stuck phase=1`** at **25 s** (`V1_SEND_STUCK_MS`) returned to inbox while **`post_wav`** could still run **~48 s** after stop — later **`upload to mazi status 200`**. UI can show “done” before server has the message, or show inbox while upload still pending.
- **Failed attempts:** `ESP_ERR_HTTP_WRITE_DATA`, “connection already in progress” — consistent with **single HTTP client contention** (hangout/PIN/WS vs large `post_wav`).

**Conclusion:** Tonight’s pain is **v1 POC upload timing and HTTP concurrency**, not 2125 boot failure and not Opus encoding. Product v1 server accepts messages when the body completes.

## Engineering artifacts touched (2026-10-23 evening)

| Change | Why |
|--------|-----|
| `scripts/flash.py` — `V1_PRODUCT_DEMOS` includes opus probe/dep | Correct `change-me-mazi` token when `WHO=mazi` |
| `firmware/secrets.sunset.h` — v1 endpoint tokens | Defaults match hangout auth |

## Private operator logs (not in git)

| Path | Contents |
|------|----------|
| `~/family-link-private/x02-2125-owner-record-20261023/` | Owner WAV send attempts; `serial-owner-record-2.txt` |
| `~/family-link-private/x02-2125-e4-20261023/` | E4 smoke + restore epoch |

## Suggested next steps (priority order)

1. **Owner decision:** promote `x02_single_factory_2125.csv` vs stay on legacy `0x300000` / dual-OTA — informed by E1–E4 + tonight.
2. **v1 firmware:** extend send stuck timeout or block other HTTP during `post_wav`; surface **send failed** vs silent inbox return.
3. **Opus in product path:** integrate **h31-style** chunk upload + server codec support in `v1_product` (today JSON create rejects `opus` codec) — separate from probe flash.
4. **Restore kit** to pre-experiment 16 MiB image when desk work ends (`restore-proof` epochs under `~/family-link-private/`).

## Kit state (end of session)

Likely still on **2125 + x02-opus-probe** with **sunset** profile; **not** automatically restored to lab09 harness unless owner ran restore.
