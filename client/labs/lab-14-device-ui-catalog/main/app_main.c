#include "lab14_catalog.h"
#include "lab14_ui.h"

#include "bsp/esp-box-3.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdio.h>

#ifndef CONFIG_LAB14_BUILD_ID
#define CONFIG_LAB14_BUILD_ID "lab14-fw-build-id-missing"
#endif
#ifndef CONFIG_LAB14_SCRIPTED
#define CONFIG_LAB14_SCRIPTED 0
#endif

#ifndef BSP_BUTTON_CONFIG_IO
#define BSP_BUTTON_CONFIG_IO GPIO_NUM_0
#endif

extern const unsigned char catalog_json_start[] asm("_binary_catalog_json_start");
extern const unsigned char catalog_json_end[] asm("_binary_catalog_json_end");

static lab14_catalog_t catalog;

static void boot_gpio_task(void *arg)
{
    (void)arg;
    bool last_up = true;
    for (;;) {
        bool up = gpio_get_level(BSP_BUTTON_CONFIG_IO) != 0;
        if (last_up && !up) {
            printf("LAB14_BOOT gpio=%d\n", (int)BSP_BUTTON_CONFIG_IO);
            if (bsp_display_lock(500)) {
                lab14_ui_handle_boot();
                bsp_display_unlock();
            } else {
                printf("LAB14_RUNTIME_FAILED reason=lvgl_lock\n");
            }
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        last_up = up;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static bool start_boot_gpio(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BSP_BUTTON_CONFIG_IO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io) != ESP_OK) {
        printf("LAB14_BOOT_WARN reason=gpio_config pin=%d\n", (int)BSP_BUTTON_CONFIG_IO);
        return false;
    }
    if (xTaskCreate(boot_gpio_task, "lab14_boot", 4096, NULL, 5, NULL) != pdPASS) {
        printf("LAB14_BOOT_WARN reason=boot_task\n");
        return false;
    }
    return true;
}

static void console_task(void *arg)
{
    (void)arg;
    char line[192];
    while (fgets(line, sizeof(line), stdin) != NULL) {
        if (!bsp_display_lock(1000)) {
            printf("LAB14_RUNTIME_FAILED reason=lvgl_lock\n");
            continue;
        }
        (void)lab14_ui_handle_line(line);
        bsp_display_unlock();
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        printf("LAB14_INIT_FAILED reason=display\n");
        return;
    }
    bsp_display_brightness_set(80);

    size_t catalog_len = (size_t)(catalog_json_end - catalog_json_start);
    if (!lab14_catalog_load(&catalog, (const char *)catalog_json_start, catalog_len)) {
        printf("LAB14_INIT_FAILED reason=catalog\n");
        return;
    }

    if (!bsp_display_lock(5000)) {
        printf("LAB14_INIT_FAILED reason=lvgl_lock\n");
        return;
    }
    bool ok = lab14_ui_init(lv_screen_active(), &catalog, CONFIG_LAB14_SCRIPTED);
    bsp_display_unlock();
    if (!ok) {
        printf("LAB14_INIT_FAILED reason=ui\n");
        return;
    }

    bool gpio_ok = start_boot_gpio();
    printf("LAB14_READY build=%s scenes=%d theme=%s boot_gpio=%d gpio_poll=%d\n",
           CONFIG_LAB14_BUILD_ID, catalog.scene_count, "THEME-DARK",
           (int)BSP_BUTTON_CONFIG_IO, gpio_ok ? 1 : 0);

    if (CONFIG_LAB14_SCRIPTED) {
        if (!bsp_display_lock(2000)) {
            printf("LAB14_RUNTIME_FAILED reason=lvgl_lock\n");
            return;
        }
        lab14_run_harness();
        bsp_display_unlock();
        return;
    }

    if (xTaskCreate(console_task, "lab14_console", 4096, NULL, 4, NULL) != pdPASS) {
        printf("LAB14_RUNTIME_FAILED reason=console_task\n");
    }
}
