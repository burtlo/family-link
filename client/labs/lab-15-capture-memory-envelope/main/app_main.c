#include "lab15_peak.h"

#include "bsp/esp-box-3.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdio.h>

#ifndef CONFIG_LAB15_BUILD_ID
#define CONFIG_LAB15_BUILD_ID "unknown"
#endif

static const char *TAG = "lab15";

static void harness_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(200));
    if (bsp_display_lock(5000)) {
        lab15_scripted_harness_run();
        bsp_display_unlock();
    } else {
        printf("LAB15_INIT_FAILED status=display_lock\n");
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
#if CONFIG_LAB15_UI_CATALOG || CONFIG_LAB15_SCRIPTED
    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        printf("LAB15_INIT_FAILED status=display_start\n");
        return;
    }
    bsp_display_brightness_set(80);
    if (!bsp_display_lock(5000)) {
        printf("LAB15_INIT_FAILED status=display_lock\n");
        return;
    }
    lv_obj_t *screen = lv_screen_active();
    if (screen == NULL) {
        printf("LAB15_INIT_FAILED status=screen\n");
        bsp_display_unlock();
        return;
    }
    lv_obj_set_scrollable(screen, false);
    bsp_display_unlock();
#else
    ESP_LOGI(TAG, "UI_NONE arm: display stack not mounted");
#endif

    int harness_flag = 0;
#if CONFIG_LAB15_SCRIPTED
    harness_flag = 1;
#endif
    printf("LAB15_READY build=%s arm=%s harness=%d\n", CONFIG_LAB15_BUILD_ID,
           lab15_arm_slug(), harness_flag);

#if CONFIG_LAB15_SCRIPTED
    xTaskCreate(harness_task, "lab15_harness", 8192, NULL, 5, NULL);
#else
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif
}
