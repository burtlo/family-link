#ifndef PRODUCT_NETWORK_H
#define PRODUCT_NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

void product_network_start(void);
/* True after STA has received an IPv4 address (may still be true while muted). */
int product_network_sta_up(void);

#ifdef __cplusplus
}
#endif

#endif
