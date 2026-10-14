#pragma once

#include <stdint.h>
#include <pthread.h>
#include <stddef.h>

typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef struct fake_queue {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    unsigned capacity;
    unsigned head;
    unsigned count;
    size_t item_size;
    unsigned char *items;
} StaticQueue_t;
typedef struct fake_task {
    pthread_t thread;
    void (*function)(void *);
    void *arg;
    int finished;
} StaticTask_t;
typedef struct fake_task *TaskHandle_t;

#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
