#ifndef LAB11_STATION_H
#define LAB11_STATION_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LAB-11-local station probe. Not a shared Wi-Fi stack and not an association. */

void lab11_station_init(void);
/* Create the TCP/IP mailbox before any HTTP client call. Does not join a network. */
bool lab11_station_prepare_transport(void);
/* Join the kit station. Credentials come from the build overlay, not the source tree. */
bool lab11_station_join(void);
void lab11_station_set_up(bool up);
bool lab11_station_is_up(void);

#ifdef __cplusplus
}
#endif
#endif
