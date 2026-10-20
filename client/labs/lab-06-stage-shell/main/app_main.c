#include "lab06_fw.h"

#include "bsp/esp-box-3.h"
#include "sdkconfig.h"

#include <stdio.h>

#ifndef CONFIG_LAB06_BUILD_ID
#define CONFIG_LAB06_BUILD_ID "lab06-fw-build-id-missing"
#endif

#ifndef CONFIG_LAB06_SCRIPTED
#define CONFIG_LAB06_SCRIPTED 0
#endif

void app_main(void)
{
    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        printf("LAB06_INIT_FAILED reason=display\n");
        return;
    }
    bsp_display_brightness_set(80);
    if (!bsp_display_lock(5000)) {
        printf("LAB06_INIT_FAILED reason=lvgl_lock\n");
        return;
    }

    if (!lab06_fw_init(lv_screen_active(),
                       CONFIG_LAB06_SCRIPTED ? "scripted" : "manual")) {
        printf("LAB06_INIT_FAILED reason=ui_or_timer\n");
        bsp_display_unlock();
        return;
    }
    bsp_display_unlock();

    printf("LAB06_READY build=%s mode=%s server=none trace_schema=lab06-trace/v1\n",
           CONFIG_LAB06_BUILD_ID,
           CONFIG_LAB06_SCRIPTED ? "scripted" : "manual");
}
