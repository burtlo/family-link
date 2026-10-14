#pragma once

#include "freertos/FreeRTOS.h"

typedef void (*TaskFunction_t)(void *);

TaskHandle_t xTaskCreateStatic(TaskFunction_t task, const char *name, uint32_t stack_words,
                               void *arg, unsigned priority, StackType_t *stack,
                               StaticTask_t *task_buffer);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
