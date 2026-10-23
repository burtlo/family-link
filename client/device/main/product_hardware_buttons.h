#ifndef PRODUCT_HARDWARE_BUTTONS_H
#define PRODUCT_HARDWARE_BUTTONS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool product_hardware_buttons_init(void);
void product_hardware_buttons_poll(void);
/* 1 when the ESP-BOX-3 mute latch (GPIO 1, active low) is on. */
int product_hardware_buttons_mute_latched(void);

#ifdef __cplusplus
}
#endif

#endif
