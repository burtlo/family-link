#pragma once

/* Copy to firmware/secrets.<profile>.h (gitignored), e.g. secrets.sunset.h and
 * secrets.anamcara.h. Flash picks the file via PROFILE=sunset|anamcara or
 * secrets_profile in kits.local.yaml for that kit's device id (from WHO=).
 *
 * Wi-Fi differs per house; server/PIN are usually shared. Device id/token here
 * are placeholders when you flash with WHO=mazi / WHO=arlo (stamped after build).
 */

#define DEMO_WIFI_SSID "your-2.4ghz-ssid"
#define DEMO_WIFI_PASS "your-password"

#define DEMO_PIN "1234"

#define DEMO_SERVER_HOST "192.168.8.143"
#define DEMO_SERVER_PORT 8080
#define DEMO_SERVER_TLS 0
#define DEMO_TLS_SKIP_VERIFY 1

#define DEMO_DEVICE_ID "box-a"
#define DEMO_DEVICE_TOKEN "change-me-a"
#define DEMO_PEER_TOKEN "change-me-b"
