# Remote endpoints (other-house boxes)

Mazi and Arlo’s BOX-3 kits live on **their** Wi-Fi. Lynn’s server runs on a **Windows PC** at home (fixed LAN IP, Ethernet). The boxes do not talk peer-to-peer; they reach the server over the internet.

**LAN tryout first:** prove `x02` + `make v1-server` on one network before shipping hardware.

---

## Recommended path: Tailscale

[Tailscale](https://tailscale.com/) gives each machine a stable `100.x` address without opening router ports.

### On the Windows server

1. Install Tailscale and sign in.
2. Note the machine’s Tailscale IP (e.g. `100.64.0.5`).
3. Run the v1 server bound to all interfaces:

   ```bash
   python -m demos.server.v1_product.server --host 0.0.0.0 --port 8080
   ```

4. Optional: use Tailscale **subnet routing** only if you need LAN devices without Tailscale — not required when each box is a Tailscale client.

### On each remote BOX-3 network

Tailscale on the **router** is ideal but rare. Practical options:

| Option | Notes |
|---|---|
| **Travel router + Tailscale** | Small router runs Tailscale; box joins its Wi-Fi |
| **Raspberry Pi / always-on host** | Tailscale subnet router for the kids’ LAN |
| **Tailscale on a phone hotspot** | Desk tryout only |

Each endpoint still needs **2.4 GHz Wi-Fi** credentials in `firmware/secrets.h` (other-house SSID).

### Firmware `secrets.h`

```c
#define DEMO_SERVER_HOST "100.64.0.5"   /* Tailscale IP of Windows server */
#define DEMO_SERVER_PORT 8080
```

Use the **Tailscale IP**, not the home LAN `192.168.x.x`, unless subnet routing is configured.

---

## TLS (before production)

Plain HTTP is OK on a trusted LAN demo. Over Tailscale, traffic is encrypted by WireGuard, but **HTTPS on the server** is still wise if you add a phone browser on untrusted networks.

1. Generate certs: [`TLS.md`](TLS.md) — `python scripts/dev_https.py --extra-name 100.64.0.5`
2. Run v1 with TLS:

   ```bash
   python -m demos.server.v1_product.server --host 0.0.0.0 --port 8443 \
     --ssl-certfile data/certs/dev.pem --ssl-keyfile data/certs/dev-key.pem
   ```

3. Firmware: set in `firmware/secrets.h`:

   ```c
   #define DEMO_SERVER_HOST "192.168.8.143"  /* or Tailscale IP */
   #define DEMO_SERVER_PORT 8443
   #define DEMO_SERVER_TLS 1
   #define DEMO_TLS_SKIP_VERIFY 1            /* desk; 0 + SNTP + CA for ship */
   ```

   Then `make x02 WHO=mazi` / `WHO=arlo`. x02 uses **h16 skip-verify** when `DEMO_TLS_SKIP_VERIFY` is 1 (`crt_bundle_attach=NULL`). Ship builds set it to 0 so x02 waits for SNTP and attaches the IDF CA bundle. See [`TLS.md`](TLS.md).

`make v1-server` is HTTP **8080**. For HTTPS: `make v1-server-tls` (port **8443**, needs `data/certs/dev.pem` per [`TLS.md`](TLS.md)).

LAN desk TLS tryout:

```bash
python scripts/dev_https.py --extra-name 192.168.8.143
make v1-server-tls
# secrets.h: DEMO_SERVER_TLS 1, DEMO_SERVER_PORT 8443, DEMO_TLS_SKIP_VERIFY 1
make x02 WHO=mazi PORT=/dev/cu.usbmodem…
```

---

## Firewall (if not using Tailscale)

If you port-forward instead:

- Forward **TCP 8080** (or **8443** for TLS) to the Windows server’s LAN IP.
- Use your **public IP** or DDNS in `DEMO_SERVER_HOST`.
- Prefer TLS with a real certificate; do not expose plain HTTP on the public internet.

---

## Checklist before boxes leave home

- [ ] `hangout.local.yaml` — real PINs and endpoint tokens
- [ ] `make demo-v1` passes on the server
- [ ] Each kit flashed with `make x02` and correct `WHO=` / token / SSID
- [ ] Remote path tested (Tailscale or port-forward) from a second network
- [ ] Lynn `/app/v1.html` reachable for PIN reset and welcome audio
- [ ] First Message plays on each user’s first login

---

## Related

- Product spec: [`plans/v1-product-spec.md`](plans/v1-product-spec.md)
- TLS details: [`TLS.md`](TLS.md)
- Server API: [`SERVER-DEMOS.md`](SERVER-DEMOS.md)
