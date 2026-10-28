#include "freertos/queue.h"
#include "freertos/task.h"
#include "trace_host_stubs.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool s_task_create_failure;
static unsigned s_task_create_count;
static unsigned s_last_task_stack_words;
static pthread_mutex_t s_sink_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_sink_changed = PTHREAD_COND_INITIALIZER;
static bool s_sink_blocked;
static bool s_sink_entered;

void lab02_trace_write_line(const char *line)
{
    pthread_mutex_lock(&s_sink_mutex);
    s_sink_entered = true;
    pthread_cond_broadcast(&s_sink_changed);
    while (s_sink_blocked) {
        pthread_cond_wait(&s_sink_changed, &s_sink_mutex);
    }
    pthread_mutex_unlock(&s_sink_mutex);
    (void)fputs(line, stdout);
}

static void *run_task(void *arg)
{
    StaticTask_t *task = arg;
    task->function(task->arg);
    task->finished = 1;
    return NULL;
}

QueueHandle_t xQueueCreateStatic(unsigned length, size_t item_size, uint8_t *storage,
                                 StaticQueue_t *queue)
{
    queue->capacity = length;
    queue->item_size = item_size;
    queue->items = storage;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->changed, NULL);
    return queue;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks)
{
    (void)ticks;
    pthread_mutex_lock(&queue->mutex);
    if (queue->count == queue->capacity) {
        pthread_mutex_unlock(&queue->mutex);
        return pdFALSE;
    }
    unsigned tail = (queue->head + queue->count) % queue->capacity;
    memcpy(queue->items + tail * queue->item_size, item, queue->item_size);
    queue->count++;
    pthread_cond_signal(&queue->changed);
    pthread_mutex_unlock(&queue->mutex);
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks)
{
    pthread_mutex_lock(&queue->mutex);
    if (queue->count == 0u && ticks != 0u) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += ticks / 1000u;
        deadline.tv_nsec += (long)(ticks % 1000u) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        while (queue->count == 0u) {
            int result = pthread_cond_timedwait(&queue->changed, &queue->mutex, &deadline);
            if (result == ETIMEDOUT)
                break;
        }
    }
    if (queue->count == 0u) {
        pthread_mutex_unlock(&queue->mutex);
        return pdFALSE;
    }
    memcpy(item, queue->items + queue->head * queue->item_size, queue->item_size);
    queue->head = (queue->head + 1u) % queue->capacity;
    queue->count--;
    pthread_mutex_unlock(&queue->mutex);
    return pdTRUE;
}

unsigned uxQueueMessagesWaiting(QueueHandle_t queue)
{
    pthread_mutex_lock(&queue->mutex);
    unsigned count = queue->count;
    pthread_mutex_unlock(&queue->mutex);
    return count;
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t task, const char *name, uint32_t stack_words,
                               void *arg, unsigned priority, StackType_t *stack,
                               StaticTask_t *task_buffer)
{
    (void)name;
    s_last_task_stack_words = stack_words;
    (void)priority;
    (void)stack;
    s_task_create_count++;
    if (s_task_create_failure) {
        return NULL;
    }
    task_buffer->function = task;
    task_buffer->arg = arg;
    task_buffer->finished = 0;
    if (pthread_create(&task_buffer->thread, NULL, run_task, task_buffer) != 0) {
        return NULL;
    }
    pthread_detach(task_buffer->thread);
    return task_buffer;
}

void trace_host_set_task_create_failure(bool fail)
{
    s_task_create_failure = fail;
}

unsigned trace_host_task_create_count(void)
{
    return s_task_create_count;
}

unsigned trace_host_last_task_stack_words(void)
{
    return s_last_task_stack_words;
}

void trace_host_set_sink_blocked(bool blocked)
{
    pthread_mutex_lock(&s_sink_mutex);
    s_sink_blocked = blocked;
    if (!blocked) {
        pthread_cond_broadcast(&s_sink_changed);
    }
    pthread_mutex_unlock(&s_sink_mutex);
}

void trace_host_reset_sink_observation(void)
{
    pthread_mutex_lock(&s_sink_mutex);
    s_sink_entered = false;
    pthread_mutex_unlock(&s_sink_mutex);
}

void trace_host_wait_sink_entered(void)
{
    pthread_mutex_lock(&s_sink_mutex);
    while (!s_sink_entered) {
        pthread_cond_wait(&s_sink_changed, &s_sink_mutex);
    }
    s_sink_entered = false;
    pthread_mutex_unlock(&s_sink_mutex);
}

void vTaskDelete(TaskHandle_t task)
{
    (void)task;
}

void vTaskDelay(TickType_t ticks)
{
    if (ticks != 0u) {
        struct timespec delay = {
            .tv_sec = ticks / 1000u,
            .tv_nsec = (long)(ticks % 1000u) * 1000000L,
        };
        nanosleep(&delay, NULL);
    }
}
