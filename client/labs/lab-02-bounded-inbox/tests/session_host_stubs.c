#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lab02/trace_emit.h"
#include "esp_timer.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
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

static QueueHandle_t s_host_fetch_queue;
unsigned lab02_host_fetch_queue_depth(void)
{
    pthread_mutex_lock(&s_host_fetch_queue->mu);
    unsigned depth = s_host_fetch_queue->count;
    pthread_mutex_unlock(&s_host_fetch_queue->mu);
    return depth;
}

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
    s_host_fetch_queue = q;
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

static SemaphoreHandle_t s_host_post_mutex;
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{ s_host_post_mutex = xSemaphoreCreateCounting(1, 1); return s_host_post_mutex; }
void lab02_host_hold_admission(bool hold)
{
    if (hold) { if (xSemaphoreTake(s_host_post_mutex, 0) != pdTRUE) abort(); }
    else xSemaphoreGive(s_host_post_mutex);
}
static bool s_hold_after_terminal;
static uint32_t s_terminal_count_op;
static unsigned s_terminal_count;
void lab02_host_hold_after_terminal(bool hold)
{ s_hold_after_terminal = hold; }
unsigned lab02_host_fetch_terminal_count(uint32_t op_id)
{ return op_id == s_terminal_count_op ? s_terminal_count : 0; }

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

static atomic_llong s_frozen_host_time;
void lab02_host_freeze_clock(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    atomic_store(&s_frozen_host_time, (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
}

int64_t esp_timer_get_time(void)
{
    int64_t frozen = atomic_load(&s_frozen_host_time);
    if (frozen) return frozen;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void lab02_trace_emit(const char *event, uint32_t generation, int focus_index,
                      const char *message_id, const char *extra)
{
    printf("SESSION_TRACE event=%s op_id=%u page=%d message=%s %s\n",
           event, (unsigned)generation, focus_index, message_id ? message_id : "-", extra ? extra : "");
}

void lab02_trace_emit_payload(const char *event, uint32_t generation, int focus_index,
                              const char *message_id,
                              const lab02_trace_payload_t *payload)
{
    if (!strcmp(event, "fetch_terminal") && payload &&
        (payload->fields & LAB02_TRACE_FIELD_OP_ID)) {
        if (s_terminal_count_op == payload->op_id) {
            s_terminal_count++;
        } else {
            s_terminal_count_op = payload->op_id;
            s_terminal_count = 1;
        }
        if (s_hold_after_terminal) {
            s_hold_after_terminal = false;
            lab02_host_hold_admission(true);
        }
    }
    printf("SESSION_TRACE event=%s op_id=%u page=%d message=%s",
           event, (unsigned)generation, focus_index, message_id ? message_id : "-");
    if (payload) {
        if (payload->fields & LAB02_TRACE_FIELD_SESSION_ID) {
            printf(" session=%u", (unsigned)payload->session_id);
        }
        if (payload->fields & LAB02_TRACE_FIELD_REVISION) {
            printf(" revision=%u", (unsigned)payload->revision);
        }
        if (payload->fields & LAB02_TRACE_FIELD_SNAP_ID) {
            printf(" snap_id=%u", (unsigned)payload->snap_id);
        }
        if (payload->fields & LAB02_TRACE_FIELD_FETCH_SNAP_ID) {
            printf(" fetch_snap_id=%u", (unsigned)payload->fetch_snap_id);
        }
        if (payload->fields & LAB02_TRACE_FIELD_ATTEMPT) {
            printf(" attempt=%u", (unsigned)payload->attempt);
        }
        if (payload->fields & LAB02_TRACE_FIELD_OP_ID) {
            printf(" op_id=%u", (unsigned)payload->op_id);
        }
        if (payload->fields & LAB02_TRACE_FIELD_OUTCOME) {
            printf(" outcome=%s", payload->outcome ? payload->outcome : "-");
        }
        if (payload->fields & LAB02_TRACE_FIELD_REASON) {
            printf(" reason=%s", payload->reason ? payload->reason : "-");
        }
        if (payload->fields & LAB02_TRACE_FIELD_ELAPSED_US) {
            printf(" elapsed_us=%lld", (long long)payload->elapsed_us);
        }
    }
    putchar('\n');
}

uint32_t lab02_snap_current_id(void)
{
    return 0;
}

#ifndef LAB02_REAL_HTTP_PROVIDER
void lab02_page_provider_http_start_io(void) {}
void lab02_page_provider_http_notify_initial_ready(uint32_t inbox_revision, uint16_t total_count)
{
    (void)inbox_revision;
    (void)total_count;
}

#endif
