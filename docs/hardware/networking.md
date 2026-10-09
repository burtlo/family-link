# Networking

**See also:** [`HARDWARE.md`](HARDWARE.md), [`TLS.md`](../TLS.md), [`REMOTE.md`](../REMOTE.md), [`plans/v1-product-spec.md`](../plans/v1-product-spec.md).

## Radio and LAN

| Topic | Label | Detail |
|---|---|---|
| Wi‑Fi bands | **Specification** | **2.4 GHz only** (802.11 b/g/n). **No 5 GHz.** |
| Cellular | **Specification** | **None** on BOX-3. |
| Architecture | **Specification** (product) | `endpoint --HTTPS/WSS--> home server <-- parent web /app` |
| SSID | **Inferred** | Preload credentials in NVS / local secrets; use **2.4 GHz** SSID when router splits bands. |
| Desk dev | **Observed** | Combined host on LAN; boxes on same Wi‑Fi as Mac server. |
| Remote endpoints | **Specification** + **Inferred** | **TLS** required; home server via **Tailscale** (or equivalent) before shipping to other house ([`TLS.md`](../TLS.md)). |

## TLS and time

| Topic | Label | Detail |
|---|---|---|
| LAN demos | **Observed** | h07–h12, server demos: **plain HTTP** OK on Mac. |
| iPhone parent mic | **Specification** (browser) | **HTTPS** secure context required for `getUserMedia` — not `http://192.168.x.x` on phone. |
| BOX certificate verify | **Observed** (h16) | Demo **skip-verify** only — **not for production**. |
| Production BOX | **Inferred** | **SNTP** after Wi‑Fi join, then CA bundle or pin ([`TLS.md`](../TLS.md)). |
| Clock without SNTP | **Specification** | Cert validation fails (1970 boot). |

## Throughput expectations (audio upload)

From [`MESSAGE-COSTS.md`](../MESSAGE-COSTS.md) — **Observed** x02 **20 s** HTTP timeout:

| Message size | Minimum sustained uplink (ideal) | **Inferred** risk |
|---|---|---|
| ~320 KB (10 s) | ~0.13 Mbps | Usually fine on LAN |
| ~5.76 MB (180 s) | ~2.31 Mbps | Fails on weak **2.4 GHz** uplink even if “Wi‑Fi works” for heartbeat |

**Specification:** Opus chunking (~16 kbps) cuts airtime; island proof in h30/h31, not x02 merge.

## What the box does not do

| Capability | Label |
|---|---|
| Peer-to-peer box ↔ box | **Specification** — all media via **server** |
| WebRTC / QUIC media | **Specification** — rejected for audio |
| DNS / routing beyond Wi‑Fi client | **Unknown** per deployment |

## Debug connectivity

| Path | Label |
|---|---|
| USB serial | **Observed** — `/dev/cu.usbmodem*` (Mac); flash via **main USB-C** |
| Wi‑Fi down at boot | **Observed** (h06) — may not join; separate from upload retry gap |
