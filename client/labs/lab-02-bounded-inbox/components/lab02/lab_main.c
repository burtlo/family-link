#include "lab02/lab_main.h"

#include "lab02/carousel_controller.h"
#include "lab02/eager_carousel.h"
#include "lab02/instrumentation.h"
#include "lab02_fixtures/fixtures.h"

#include "lab02_platform/board.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "lab02_main";

#if CONFIG_LAB_AUTO_HARNESS
extern void lab02_harness_start(void);
#endif

static const char *fixture_name(void)
{
#if CONFIG_LAB02_FIXTURE_8
    return "inbox_8";
#elif CONFIG_LAB02_FIXTURE_16
    return "inbox_16";
#elif CONFIG_LAB02_FIXTURE_64
    return "inbox_64";
#elif CONFIG_LAB02_FIXTURE_256
    return "inbox_256";
#else
    return "inbox_16";
#endif
}

static const char *mode_name(void)
{
#if CONFIG_LAB02_EAGER_BASELINE
    return "eager";
#else
    if (strlen(CONFIG_LAB02_SERVER_URL) > 0) {
        return "http";
    }
    return "recycle";
#endif
}

void lab02_app_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    lab02_instrumentation_reset();

    if (lab02_board_display_start() != ESP_OK) {
        ESP_LOGE(TAG, "display start failed");
        return;
    }

    const esp_app_desc_t *desc = esp_app_get_description();
    const char *build_id = desc && desc->version[0] ? desc->version : "dev";
    const lab02_fixture_blob_t *blob = lab02_fixture_selected();
    const char *fixture_rev = blob && blob->name ? blob->name : fixture_name();

    printf("LAB02_READY build=%s fixture=%s mode=%s\n", build_id, fixture_rev, mode_name());
    ESP_LOGI(TAG, "LAB02_READY build=%s fixture=%s mode=%s", build_id, fixture_rev, mode_name());

    if (!lab02_board_lvgl_lock(5000)) {
        ESP_LOGE(TAG, "LVGL lock timeout");
        return;
    }

#if CONFIG_LAB02_EAGER_BASELINE
    if (!lab02_eager_carousel_start()) {
        lab02_board_status_set("eager init failed");
    }
#else
    static lab02_carousel_t carousel;
    if (!lab02_carousel_init(&carousel)) {
        lab02_board_status_set("carousel init failed");
    }
#endif

    lab02_board_lvgl_unlock();

#if CONFIG_LAB_AUTO_HARNESS
    lab02_harness_start();
#endif
}
