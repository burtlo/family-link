#include "lab10_ui_lvgl.h"

#include "bsp/esp-box-3.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "sdkconfig.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#ifndef CONFIG_LAB10_BUILD_ID
#define CONFIG_LAB10_BUILD_ID "lab10-render-l10-04-segment-default"
#endif

static const char *TAG = "lab10_ui";
static lab10_ui_view_t s_view;
static atomic_bool s_physical_button_down;
static button_handle_t s_buttons[BSP_BUTTON_NUM];

static void physical_button_down(void *button, void *context)
{
    (void)button;
    (void)context;
    /* The BSP callback never touches LVGL. The UI timer consumes this flag. */
    atomic_store_explicit(&s_physical_button_down, true, memory_order_release);
}

static bool init_physical_button(void)
{
    int button_count = 0;
    if (bsp_iot_button_create(s_buttons, &button_count, BSP_BUTTON_NUM) != ESP_OK ||
        s_buttons[BSP_BUTTON_MAIN] == NULL) {
        ESP_LOGW(TAG, "physical ring-wipe entry unavailable on this BSP");
        return false;
    }
    if (iot_button_register_cb(s_buttons[BSP_BUTTON_MAIN], BUTTON_PRESS_DOWN,
                               NULL, physical_button_down, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "physical ring-wipe callback registration failed");
        return false;
    }
    ESP_LOGI(TAG, "physical main button enables ring-wipe entry");
    return true;
}

static void ui_poll_timer(lv_timer_t *timer)
{
    lab10_ui_view_t *view = (lab10_ui_view_t *)lv_timer_get_user_data(timer);
    if (view == NULL) {
        return;
    }
    if (atomic_exchange_explicit(&s_physical_button_down, false,
                                 memory_order_acq_rel)) {
        if (lab10_ui_state(&view->model) == LAB10_SCREEN_IDLE) {
            lab10_ui_view_enter_ring_wipe(view, 160, 120);
        } else if (lab10_ui_state(&view->model) == LAB10_SCREEN_RECORDING) {
            /* Stop owns the capture terminal event; the next press exits. */
            lab10_ui_view_stop_capture(view);
        } else if (lab10_ui_state(&view->model) == LAB10_SCREEN_STOPPED ||
                   lab10_ui_state(&view->model) == LAB10_SCREEN_CANCELLED ||
                   lab10_ui_state(&view->model) == LAB10_SCREEN_FAILED ||
                   lab10_ui_state(&view->model) == LAB10_SCREEN_COMPLETED) {
            lab10_ui_view_exit_reverse(view);
        }
    }
    /* The bounded scripted workload runs on this LVGL timer/UI task. */
    lab10_ui_view_scripted_poll(view);
    lab10_ui_view_poll(view);
}

void app_main(void)
{
    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        printf("LAB10_INIT_FAILED display\n");
        return;
    }
    bsp_display_brightness_set(80);
    if (!bsp_display_lock(5000)) {
        printf("LAB10_INIT_FAILED lvgl_lock\n");
        return;
    }

    lv_obj_t *screen = lv_screen_active();
    if (screen == NULL) {
        printf("LAB10_INIT_FAILED screen\n");
        bsp_display_unlock();
        return;
    }
    lv_obj_set_scrollable(screen, false);
    if (!lab10_ui_view_init(&s_view, screen)) {
        printf("LAB10_INIT_FAILED ui_view\n");
        bsp_display_unlock();
        return;
    }
    s_view.poll_timer = lv_timer_create(ui_poll_timer, 20, &s_view);
    if (s_view.poll_timer == NULL) {
        printf("LAB10_INIT_FAILED ui_timer\n");
        lab10_ui_view_deinit(&s_view);
        bsp_display_unlock();
        return;
    }
    lab10_ui_view_enter_fab_bloom(&s_view, LAB10_FAB_ORIGIN_X,
                                  LAB10_FAB_ORIGIN_Y);
    bsp_display_unlock();

    const bool ring_available = init_physical_button();
    lab10_ui_view_set_physical_ring_available(&s_view, ring_available);
    printf("LAB10_READY build=%s trace_schema=lab10-record-trace/v1 physical_ring_wipe=%d\n",
           CONFIG_LAB10_BUILD_ID, ring_available ? 1 : 0);

    for (;;) {
        uint32_t scripted_accepted_points;
        lab10_capture_terminal_outcome_t terminal_outcome;

        /* Drain in lifecycle order. These atomic one-slot handoffs are
         * produced by LVGL but all serial output stays on this service task.
         */
        if (lab10_ui_view_take_pending_scripted_start()) {
            printf("LAB10_SCRIPTED_DRAWING status=started source=ui_task "
                   "hardware_touch=0 duration_cap_ms=180000 "
                   "move_interval_ms=%lu accepted_point_cap=%u "
                   "palette_changes=0 strokes=1 color=white\n",
                   (unsigned long)(LAB10_UI_SCRIPTED_MOVE_INTERVAL_US / 1000u),
                   (unsigned)LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY);
        }
        if (lab10_ui_view_take_pending_scripted_completion(
                &scripted_accepted_points)) {
            printf("LAB10_SCRIPTED_DRAWING status=complete source=ui_task "
                   "hardware_touch=0 terminal=duration_cap duration_cap_ms=180000 "
                   "accepted_points=%" PRIu32 " strokes=1 palette_changes=0 color=white\n",
                   scripted_accepted_points);
        }
        if (lab10_ui_view_take_pending_terminal_outcome(&terminal_outcome)) {
            printf("LAB10_CAPTURE_TERMINAL outcome=%s terminal_event_count=1\n",
                   lab10_capture_outcome_name(terminal_outcome));
        }
        /* Service terminal events promptly without spinning when none exist. */
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
