#pragma once

#include "freertos/FreeRTOS.h"

typedef struct event_group_stub *EventGroupHandle_t;

EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits_to_wait_for,
                                int clear_on_exit, int wait_for_all, TickType_t ticks_to_wait);
void xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits_to_clear);
void xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits_to_set);
