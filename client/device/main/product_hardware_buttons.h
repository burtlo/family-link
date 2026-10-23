#ifndef PRODUCT_HARDWARE_BUTTONS_H
#define PRODUCT_HARDWARE_BUTTONS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool product_hardware_buttons_init(void);
void product_hardware_buttons_poll(void);

#ifdef __cplusplus
}
#endif

#endif
