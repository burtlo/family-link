#include "sdkconfig.h"

#if CONFIG_LAB05_PEAK

#include "lab05_peak_harness.h"

void app_main(void)
{
    lab05_peak_harness_main();
}

#else

#include <stdio.h>

#include "bsp/esp-bsp.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#ifndef CONFIG_LAB05_BUILD_ID
#define CONFIG_LAB05_BUILD_ID "unknown"
#endif

#if CONFIG_LAB05_SCRIPTED
#define LAB05_MODE "scripted"
#else
#define LAB05_MODE "manual"
#endif

static const char *TAG = "lab05";
static lv_obj_t *s_screens[2];
static unsigned s_state;

static void transition_to(unsigned next)
{
    if (next > 1 || next == s_state) {
        return;
    }
    unsigned previous = s_state;
    s_state = next;
    printf("LAB05_TRANSITION source=%s from=%u to=%u build=%s\n",
#if CONFIG_LAB05_SCRIPTED
           "harness",
#else
           "touch",
#endif
           previous, next, CONFIG_LAB05_BUILD_ID);
    lv_screen_load_anim(s_screens[next], next > previous ? LV_SCR_LOAD_ANIM_MOVE_LEFT
                                                         : LV_SCR_LOAD_ANIM_MOVE_RIGHT,
                        250, 0, false);
}

static void on_next(lv_event_t *event)
{
    unsigned next = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    transition_to(next);
}

static lv_obj_t *make_screen(unsigned state)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(state == 0 ? 0x10212D : 0x26351C), 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, state == 0 ? "LAB-05  /  READY" : "LAB-05  /  ACTIVE");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -28);

    lv_obj_t *button = lv_button_create(screen);
    lv_obj_set_size(button, 150, 56);
    lv_obj_align(button, LV_ALIGN_CENTER, 0, 42);
    lv_obj_add_event_cb(button, on_next, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)(state == 0 ? 1 : 0));
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, state == 0 ? "NEXT" : "BACK");
    lv_obj_center(label);
    return screen;
}

#if CONFIG_LAB05_SCRIPTED
static void scripted_harness(void *unused)
{
    (void)unused;
    vTaskDelay(pdMS_TO_TICKS(2500));
    if (bsp_display_lock(1000)) {
        printf("LAB05_HARNESS_BEGIN build=%s\n", CONFIG_LAB05_BUILD_ID);
        transition_to(1);
        printf("LAB05_HARNESS_DONE status=transitioned build=%s\n", CONFIG_LAB05_BUILD_ID);
        bsp_display_unlock();
    } else {
        printf("LAB05_HARNESS_DONE status=display_lock_timeout build=%s\n",
               CONFIG_LAB05_BUILD_ID);
    }
    vTaskDelete(NULL);
}
#endif

void app_main(void)
{
    if (bsp_display_start() == NULL) {
        ESP_LOGE(TAG, "display_start_failed");
        printf("LAB05_READY build=%s mode=%s status=display_start_failed\n",
               CONFIG_LAB05_BUILD_ID, LAB05_MODE);
        return;
    }
    bsp_display_brightness_set(80);
    if (!bsp_display_lock(1000)) {
        ESP_LOGE(TAG, "display_lock_failed");
        return;
    }
    s_screens[0] = make_screen(0);
    s_screens[1] = make_screen(1);
    lv_screen_load(s_screens[0]);
    bsp_display_unlock();

    printf("LAB05_READY build=%s mode=%s server=none\n", CONFIG_LAB05_BUILD_ID, LAB05_MODE);
#if CONFIG_LAB05_SCRIPTED
    xTaskCreate(scripted_harness, "lab05_harness", 3072, NULL, 4, NULL);
#endif
}

#endif
