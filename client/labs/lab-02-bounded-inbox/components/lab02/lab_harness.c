#include "lab02/carousel_controller.h"
#include "lab02/lab02_session.h"
#include "lab02/trace_emit.h"
#include "lab02_platform/board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

#if CONFIG_LAB_AUTO_HARNESS

extern lab02_carousel_t *lab02_carousel_instance(void);

static void harness_show_step(int step_one_based, int total, const char *who)
{
    char line[56];
    snprintf(line, sizeof(line), "AUTO %d/%d  %s", step_one_based, total, who ? who : "—");
    lab02_board_status_set(line);
}

/** Wait for HTTP boot (or fixture attach) so item_count is not read at shell-only zero. */
static bool harness_wait_inbox(lab02_carousel_t *c, int max_wait_ms)
{
    int waited = 0;
    while (waited < max_wait_ms) {
        if (c && lab02_board_lvgl_lock(200)) {
            lab02_carousel_pump(c);
            lab02_board_lvgl_unlock();
        }
        lab02_session_snapshot_t snap = lab02_session_snapshot();
        if (snap.load_stage == LAB02_LOAD_ERROR) {
            return false;
        }
        if (snap.load_stage == LAB02_LOAD_READY) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
        waited += 250;
    }
    lab02_session_snapshot_t snap = lab02_session_snapshot();
    return snap.load_stage == LAB02_LOAD_READY;
}

static void harness_step(lab02_carousel_t *c, int index, int step_one_based, int total)
{
    const char *who = "—";
    if (lab02_board_lvgl_lock(500)) {
        lab02_carousel_set_focus_with_origin(c, index, true, "harness");
        const lab02_inbox_item_t *item = lab02_page_provider_item(&c->provider, c->focus_index);
        if (item && item->sender_name[0]) {
            who = item->sender_name;
        }
        lab02_board_lvgl_unlock();
    }
    harness_show_step(step_one_based, total, who);
    printf("HARNESS_STEP focus=%d (%d/%d) %s\n", index, step_one_based, total, who);
}

/** Keep scripted traversal on the real animation path, then dwell without holding LVGL. */
static bool harness_wait_snap(lab02_carousel_t *c, int max_wait_ms)
{
    int waited = 0;
    while (waited < max_wait_ms) {
        bool locked = false;
        if (lab02_board_lvgl_lock(100)) {
            lab02_carousel_pump(c);
            locked = c->snap.scroll_lock;
            lab02_board_lvgl_unlock();
        }
        if (!locked) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        waited += 20;
    }
    return false;
}

static void harness_task(void *arg)
{
    lab02_carousel_t *c = lab02_carousel_instance();
    (void)arg;
    printf("HARNESS_WAIT boot settle...\n");
    vTaskDelay(pdMS_TO_TICKS(800));
    if (!c) {
        printf("HARNESS_DONE build=%s status=no_carousel\n", CONFIG_LAB02_BUILD_ID);
        vTaskDelete(NULL);
        return;
    }
    if (!harness_wait_inbox(c, 30000)) {
        printf("HARNESS_DONE build=%s status=inbox_not_ready\n", CONFIG_LAB02_BUILD_ID);
        vTaskDelete(NULL);
        return;
    }
    if (lab02_board_lvgl_lock(500)) {
        lab02_carousel_pump(c);
        lab02_board_lvgl_unlock();
    }
    int n = c->item_count;
    int legs = n < 20 ? n : 20;
    int total_steps = legs * 2;
    printf("HARNESS_BEGIN legs=%d total_steps=%d\n", legs, total_steps);
    harness_show_step(0, legs, "starting");
    for (int i = 0; i < legs; i++) {
        harness_step(c, i % n, i + 1, legs);
        if (!harness_wait_snap(c, 1500)) {
            printf("HARNESS_DONE build=%s status=snap_timeout\n", CONFIG_LAB02_BUILD_ID);
            vTaskDelete(NULL);
            return;
        }
        if (i + 1 == 2 &&
            (strcmp(CONFIG_LAB02_SCENARIO, "S-push") == 0 ||
             strcmp(CONFIG_LAB02_SCENARIO, "S-rm-focus") == 0)) {
            printf("HARNESS_SCENARIO_DWELL scenario=%s ms=%d\n",
                   CONFIG_LAB02_SCENARIO, CONFIG_LAB02_SCENARIO_DWELL_MS);
            vTaskDelay(pdMS_TO_TICKS(CONFIG_LAB02_SCENARIO_DWELL_MS));
        }
        vTaskDelay(pdMS_TO_TICKS(700));
    }
    for (int i = legs - 1; i >= 0; i--) {
        harness_step(c, i % n, legs - i, legs);
        if (!harness_wait_snap(c, 1500)) {
            printf("HARNESS_DONE build=%s status=snap_timeout\n", CONFIG_LAB02_BUILD_ID);
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    lab02_board_status_set("");
    lab02_trace_emit("harness_done", 0, c->focus_index, NULL, NULL);
    printf("HARNESS_DONE build=%s steps=%d scenario=%s\n", CONFIG_LAB02_BUILD_ID, total_steps,
           CONFIG_LAB02_SCENARIO);
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
