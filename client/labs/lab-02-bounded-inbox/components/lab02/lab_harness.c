#include "lab02/carousel_controller.h"
#include "lab02/trace_emit.h"
#include "lab02_platform/board.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

#if CONFIG_LAB_AUTO_HARNESS

extern lab02_carousel_t *lab02_carousel_instance(void);

static void harness_task(void *arg)
{
    lab02_carousel_t *c = lab02_carousel_instance();
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500));
    if (!c) {
        printf("HARNESS_DONE status=no_carousel\n");
        vTaskDelete(NULL);
        return;
    }
    int n = c->item_count;
    int steps = n < 20 ? n : 20;
    for (int i = 0; i < steps; i++) {
        if (lab02_board_lvgl_lock(500)) {
            lab02_carousel_set_focus(c, i % n, true);
            lab02_board_lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(400));
    }
    for (int i = steps - 1; i >= 0; i--) {
        if (lab02_board_lvgl_lock(500)) {
            lab02_carousel_set_focus(c, i % n, true);
            lab02_board_lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    lab02_trace_emit("harness_done", 0, c->focus_index, NULL, NULL);
    printf("HARNESS_DONE steps=%d\n", steps);
    vTaskDelete(NULL);
}

void lab02_harness_start(void)
{
    xTaskCreate(harness_task, "lab02_harness", 4096, NULL, 5, NULL);
}

#else

void lab02_harness_start(void)
{
}

#endif
