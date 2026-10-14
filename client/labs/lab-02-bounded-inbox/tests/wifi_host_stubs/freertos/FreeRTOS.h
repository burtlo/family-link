#pragma once

#include <stdint.h>

typedef uint32_t EventBits_t;
typedef uint32_t TickType_t;

#define BIT0 (1u << 0)
#define pdFALSE 0
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
