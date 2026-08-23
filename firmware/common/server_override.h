#pragma once

/*
 * Optional CMake -D FAMILY_SERVER_* overrides so `make demo-cross-heartbeat`
 * can point the box at this Mac's LAN IP (or a tunnel host) without editing
 * secrets.h. Wi-Fi SSID/password still come from secrets.h.
 */

#ifdef FAMILY_SERVER_HOST
#undef DEMO_SERVER_HOST
#define DEMO_SERVER_HOST FAMILY_SERVER_HOST
#endif

#ifdef FAMILY_SERVER_PORT
#undef DEMO_SERVER_PORT
#define DEMO_SERVER_PORT FAMILY_SERVER_PORT
#endif

#ifdef FAMILY_SERVER_TLS
#undef DEMO_SERVER_TLS
#define DEMO_SERVER_TLS FAMILY_SERVER_TLS
#endif
