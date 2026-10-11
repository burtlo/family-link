#pragma once

#include "freertos/FreeRTOS.h"
#include <stddef.h>

typedef struct fake_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned length, size_t item_size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, uint32_t ticks);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, uint32_t ticks);
BaseType_t xQueuePeek(QueueHandle_t queue, void *item, uint32_t ticks);
