#pragma once

#include "freertos/FreeRTOS.h"

typedef struct fake_sem *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateCounting(unsigned max_count, unsigned initial_count);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, uint32_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
