#pragma once

#include "freertos/FreeRTOS.h"
#include <stddef.h>

typedef struct fake_queue *QueueHandle_t;

QueueHandle_t xQueueCreateStatic(unsigned length, size_t item_size, uint8_t *storage,
                                 StaticQueue_t *queue);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks);
unsigned uxQueueMessagesWaiting(QueueHandle_t queue);
