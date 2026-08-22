#pragma once

/* Copy to secrets.h (gitignored) and fill in. Never commit real values. */

#define DEMO_WIFI_SSID "your-2.4ghz-ssid"
#define DEMO_WIFI_PASS "your-password"

#define DEMO_PIN "1234"

#define DEMO_SERVER_HOST "192.168.1.10"
#define DEMO_SERVER_PORT 8080
/* 0 = HTTP demos (h07–h15). h16 always uses https:// and ignores this. */
#define DEMO_SERVER_TLS 0
#define DEMO_DEVICE_ID "box-a"
#define DEMO_DEVICE_TOKEN "change-me-a"
/* box-b token, used by h10 to seed the inbox when the Mac is not posting. */
#define DEMO_PEER_TOKEN "change-me-b"
