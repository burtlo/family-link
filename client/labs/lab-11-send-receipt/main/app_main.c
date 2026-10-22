#include "lab11_fw.h"

#include "lab11_send.h"
#include "sdkconfig.h"

#include <stdio.h>

#ifndef CONFIG_LAB11_BUILD_ID
#define CONFIG_LAB11_BUILD_ID "lab11-fw-build-id-missing"
#endif

#ifndef CONFIG_LAB11_SCRIPTED
#define CONFIG_LAB11_SCRIPTED 0
#endif

void app_main(void)
{
    char line[160];
    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        lab11_format_boot(line, sizeof(line), false, CONFIG_LAB11_BUILD_ID, "manual", 0);
        printf("%s\n", line);
        return;
    }
    bsp_display_brightness_set(80);
    if (!bsp_display_lock(5000)) {
        lab11_format_boot(line, sizeof(line), false, CONFIG_LAB11_BUILD_ID, "manual", 0);
        printf("%s\n", line);
        return;
    }
    if (!lab11_fw_init(lv_screen_active(), CONFIG_LAB11_SCRIPTED ? "scripted" : "manual")) {
        lab11_format_boot(line, sizeof(line), false, CONFIG_LAB11_BUILD_ID, "manual", 0);
        printf("%s\n", line);
        bsp_display_unlock();
        return;
    }
    bsp_display_unlock();
    lab11_format_boot(line, sizeof(line), true, CONFIG_LAB11_BUILD_ID,
                      CONFIG_LAB11_SCRIPTED ? "scripted" : "manual",
                      CONFIG_LAB11_SCRIPTED ? 1 : 0);
    printf("%s\n", line);
    lab11_fw_start();
}
