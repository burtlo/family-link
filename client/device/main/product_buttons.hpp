#pragma once

namespace product_buttons
{

bool init();
void poll();
/* 1 when the ESP-BOX-3 mute latch (GPIO 1, active low) is on. */
int mute_latched();
/* Raw GPIO level (0/1), or -1 if GPIO was not configured. */
int mute_gpio_level();
/* iot_button key level (0/1), or -1 if no mute handle. */
int mute_button_level();
int mute_gpio_ready();

} /* namespace product_buttons */
