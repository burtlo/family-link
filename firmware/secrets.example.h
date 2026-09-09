#pragma once

/* Copy to secrets.h (gitignored) and fill in. Never commit real values. */

#define DEMO_WIFI_SSID "your-2.4ghz-ssid"
#define DEMO_WIFI_PASS "your-password"

#define DEMO_PIN "1234"

#define DEMO_SERVER_HOST "192.168.8.143"
#define DEMO_SERVER_PORT 8080
/* 0 = HTTP (LAN desk). 1 = HTTPS; pair with make v1-server-tls on :8443. */
#define DEMO_SERVER_TLS 0
/* 1 = h16-style skip-verify (desk). 0 = CA bundle + SNTP (ship). */
#define DEMO_TLS_SKIP_VERIFY 1
/* Two-box open line (h20–h22, h26, h27): Mazi = box-a / change-me-a, Arlo = box-b /
   change-me-b. Same firmware binary; `make flash WHO=mazi` / `WHO=arlo` stamps
   identity. Display names come from the host registry, not this header. */
/* Two-box open line (h20–h22, h26, h27): Mazi = box-a / change-me-a, Arlo = box-b /
   change-me-b. v1 product (x02): use hangout.example.yaml endpoint tokens, e.g.
   endpoint-mazi / change-me-mazi. */
#define DEMO_DEVICE_ID "box-a"
#define DEMO_DEVICE_TOKEN "change-me-a"
#define DEMO_PEER_TOKEN "change-me-b"
