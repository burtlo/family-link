#pragma once

/* Join the desk network when a kit build supplied credentials. Empty SSID means a loopback build. */
bool join_desk_wifi(const char *ssid, const char *password);
