#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lab02/trace_emit.h"
#include "esp_timer.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct fake_queue {
    pthread_mutex_t mu;
    pthread_cond_t changed;
    unsigned cap;
    unsigned head;
    unsigned count;
    size_t item_size;
    unsigned char *items;
};

QueueHandle_t xQueueCreate(unsigned length, size_t item_size)
{
    struct fake_queue *q = calloc(1, sizeof(*q));
    if (!q) return NULL;
    q->items = calloc(length, item_size);
    if (!q->items) { free(q); return NULL; }
    q->cap = length;
    q->item_size = item_size;
    pthread_mutex_init(&q->mu, NULL);
    pthread_cond_init(&q->changed, NULL);
    return q;
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, uint32_t ticks)
{
    pthread_mutex_lock(&q->mu);
    while (q->count == q->cap && ticks == portMAX_DELAY) {
        pthread_cond_wait(&q->changed, &q->mu);
    }
    if (q->count == q->cap) { pthread_mutex_unlock(&q->mu); return pdFALSE; }
    unsigned tail = (q->head + q->count) % q->cap;
    memcpy(q->items + tail * q->item_size, item, q->item_size);
    q->count++;
    pthread_cond_broadcast(&q->changed);
    pthread_mutex_unlock(&q->mu);
    return pdTRUE;
}

static BaseType_t queue_read(QueueHandle_t q, void *item, uint32_t ticks, int remove)
{
    pthread_mutex_lock(&q->mu);
    while (q->count == 0 && ticks == portMAX_DELAY) {
        pthread_cond_wait(&q->changed, &q->mu);
    }
    if (q->count == 0) { pthread_mutex_unlock(&q->mu); return pdFALSE; }
    memcpy(item, q->items + q->head * q->item_size, q->item_size);
    if (remove) { q->head = (q->head + 1) % q->cap; q->count--; }
    pthread_cond_broadcast(&q->changed);
    pthread_mutex_unlock(&q->mu);
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, uint32_t ticks)
{ return queue_read(q, item, ticks, 1); }
BaseType_t xQueuePeek(QueueHandle_t q, void *item, uint32_t ticks)
{ return queue_read(q, item, ticks, 0); }

struct fake_sem {
    pthread_mutex_t mu;
    pthread_cond_t changed;
    unsigned cap;
    unsigned count;
};

SemaphoreHandle_t xSemaphoreCreateCounting(unsigned max_count, unsigned initial_count)
{
    struct fake_sem *sem = calloc(1, sizeof(*sem));
    if (!sem) return NULL;
    pthread_mutex_init(&sem->mu, NULL);
    pthread_cond_init(&sem->changed, NULL);
    sem->cap = max_count;
    sem->count = initial_count;
    return sem;
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{ return xSemaphoreCreateCounting(1, 0); }

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, uint32_t ticks)
{
    pthread_mutex_lock(&sem->mu);
    while (sem->count == 0 && ticks == portMAX_DELAY) {
        pthread_cond_wait(&sem->changed, &sem->mu);
    }
    if (sem->count == 0) { pthread_mutex_unlock(&sem->mu); return pdFALSE; }
    sem->count--;
    pthread_mutex_unlock(&sem->mu);
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem)
{
    pthread_mutex_lock(&sem->mu);
    if (sem->count == sem->cap) { pthread_mutex_unlock(&sem->mu); return pdFALSE; }
    sem->count++;
    pthread_cond_broadcast(&sem->changed);
    pthread_mutex_unlock(&sem->mu);
    return pdTRUE;
}

int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void lab02_trace_emit(const char *event, uint32_t generation, int focus_index,
                      const char *message_id, const char *extra)
{
    (void)event; (void)generation; (void)focus_index; (void)message_id; (void)extra;
}

void lab02_page_provider_http_start_io(void) {}
