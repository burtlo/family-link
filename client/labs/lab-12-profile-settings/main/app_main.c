#include "lab12_ui.h"
#include "lab12_fixture_client.h"
#include "bsp/esp-box-3.h"
#include "sdkconfig.h"
#include <stdio.h>

#ifndef CONFIG_LAB12_BUILD_ID
#define CONFIG_LAB12_BUILD_ID "lab12-fw-build-id-missing"
#endif
#ifndef CONFIG_LAB12_SCRIPTED
#define CONFIG_LAB12_SCRIPTED 0
#endif
#ifndef CONFIG_LAB12_PHASE_SHARED
#define CONFIG_LAB12_PHASE_SHARED 0
#endif

void app_main(void)
{
    const char *phase = CONFIG_LAB12_PHASE_SHARED ? "shared" : "visual";
    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        printf("LAB12_INIT_FAILED reason=display\n");
        return;
    }
    bsp_display_brightness_set(80);
    if (!bsp_display_lock(5000)) {
        printf("LAB12_INIT_FAILED reason=lvgl_lock\n");
        return;
    }
    bool ok = lab12_ui_init(lv_screen_active(), CONFIG_LAB12_PHASE_SHARED, CONFIG_LAB12_SCRIPTED);
    bsp_display_unlock();
    if (!ok) {
        printf("LAB12_INIT_FAILED reason=ui\n");
        return;
    }
    if (CONFIG_LAB12_PHASE_SHARED) {
        lab12_profile_t profile = {0};
        if (!lab12_fixture_wifi_connect() || !lab12_fixture_get_profile("endpoint-a", &profile)) {
            if (bsp_display_lock(1000)) {
                lab12_ui_profile_unavailable();
                bsp_display_unlock();
            }
            printf("LAB12_RUNTIME_FAILED reason=fixture_transport\n");
            return;
        }
        if (!bsp_display_lock(1000)) {
            printf("LAB12_RUNTIME_FAILED reason=lvgl_lock\n");
            return;
        }
        bool profile_ok = lab12_ui_profile_ready(&profile);
        bsp_display_unlock();
        if (!profile_ok) {
            printf("LAB12_RUNTIME_FAILED reason=profile_worker\n");
            return;
        }
    }
    printf("LAB12_READY build=%s phase=%s server=%s layout=split-pane\n", CONFIG_LAB12_BUILD_ID,
           phase, CONFIG_LAB12_PHASE_SHARED ? "lab12-profile/v1" : "none");
    if (CONFIG_LAB12_SCRIPTED)
        lab12_run_harness();
}
