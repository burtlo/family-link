# TLS / HTTPS for demos

LAN protocol demos (h07–h12, server 01–06) stay **plain HTTP**. This overlay is for iPhone Safari and for proving the box can speak HTTPS.

## Why iPhone needs HTTPS

Safari `getUserMedia` (mic for live PTT, sometimes camera) is treated as a powerful API. Browsers only expose it on a **secure context**:

- **HTTPS** on a real name or LAN IP
- **http://localhost** (and `127.0.0.1`) — exception on **Mac Safari / desktop Chrome**, not on the phone

An iPhone talking to `http://192.168.x.x:8080` will not get a mic. The parent page must be served over TLS before a tryout on the phone. Mac-only demos can keep HTTP.

## Why the ESP32 needs a clock

Certificate validity is a time window. With no RTC, a BOX-3 boots at 1970 unless it has **SNTP** (or another time source). mbedTLS then rejects a “not yet valid” / expired server cert even when the CA is correct.

h16 **does not** sync time. It skip-verifies the server cert (LAN demo only). Production firmware: SNTP after Wi-Fi, then verify with a CA bundle (or a pinned cert).

## What is OK on this LAN vs production

| Mode | Server cert | Phone | Box |
|---|---|---|---|
| h07 HTTP | none | Mac localhost OK; iPhone mic **no** | HTTP client |
| h16 HTTPS skip-verify | mkcert or throwaway self-signed | iPhone still warns unless mkcert CA is trusted **on the phone** | TLS handshake, **no** CA check |
| Production | Let's Encrypt / real CA, or mkcert CA installed on the phone | Trusted HTTPS | SNTP + CA bundle (or pin) |

Skip-verify is **only** for desk demos (`CONFIG_ESP_TLS_INSECURE` and `CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY` in `firmware/sdkconfig.defaults`; h16 also sets `.crt_bundle_attach = NULL`). If a local `firmware/sdkconfig` already exists, confirm skip-verify is **y** or h16 will reject the throwaway cert. Do not ship that.

## Host: certs + uvicorn

```
# preferred: mkcert (trusted on this Mac after mkcert -install)
python scripts/dev_https.py --extra-name YOUR_LAN_IP

# no mkcert: print brew hint, or a 14-day openssl cert for the box
python scripts/dev_https.py --insecure-self-signed --extra-name YOUR_LAN_IP
```

Install mkcert on this Mac:

```
brew install mkcert nss
mkcert -install
```

PEMs land in gitignored `data/certs/` (`dev.pem` + `dev-key.pem`). Never commit them.

The script prefers **combined** on **8443** (GET /v1/me plus inbox/WS/`/app`). Same flags by hand:

```
python -m demos.server.combined.server --host 0.0.0.0 --port 8443 \
  --ssl-certfile data/certs/dev.pem --ssl-keyfile data/certs/dev-key.pem
```

`01_auth` accepts the same flags if you only need `/v1/me`:

```
python -m demos.server.01_auth.server --host 0.0.0.0 --port 8443 \
  --ssl-certfile data/certs/dev.pem --ssl-keyfile data/certs/dev-key.pem
```

`--extra-name` should include the LAN IP (and any `.local` name) so a **phone** does not hit a name mismatch. h16 skip-verify does not care about SAN.

## Firmware h16

Same as h07 (good bearer `GET /v1/me`, then a bad token) over `https://DEMO_SERVER_HOST:DEMO_SERVER_PORT`.

In `firmware/secrets.h` point the port at 8443 for this flash only. Leave `DEMO_SERVER_TLS` at 0; **h07 stays HTTP**. h16 always uses `https://`.

```
python scripts/dev_https.py --extra-name YOUR_LAN_IP
make flash DEMO=h16
```

UART: skip-verify is logged; `-- PASS h16` after 200 + `device_id` then 401. TLS/transport errors print `-- FAIL h16 <reason>`.
