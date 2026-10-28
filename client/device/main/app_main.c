#include "bsp/esp-box-3.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "product_app_start.h"
#include "product_display.h"
#include "product_hardware_buttons.h"
#include "product_mem_probe.h"
#include "product_mic_capture.h"
#include "product_record_fw.h"

#include <stdbool.h>
#include <stdio.h>

#define BOOT_PHASE_LABEL_MAX 24

static bool boot_step_ok(const char *step, bool ok);
static bool boot_step_ptr(const char *step, const void *ptr);
static void boot_step_note(const char *step);
static void boot_init_step_label(const char *phase, char *step, size_t step_len);
static void boot_probe_before(const char *phase);
static void boot_probe_after(const char *phase, bool lvgl_pool);
static bool boot_phase_done(const char *phase, bool lvgl_after);
static bool boot_phase(const char *phase, bool (*init)(void));
static bool boot_phase_display(void);
static bool boot_nvs_init(void);
static void boot_backlight_after_display(void);
static void boot_log_identity(void);

void app_main(void)
{
    boot_log_identity();

    if (!boot_phase("storage", product_record_mount_storage)) {
        return;
    }
    if (!boot_phase("mic", product_mic_init)) {
        return;
    }
    if (!boot_phase_display()) {
        return;
    }

    boot_backlight_after_display();

    /* NVS is on-chip flash (Wi-Fi creds, desk token, volume prefs), not the SD card
     * mounted in boot_phase("storage", …). */
    if (!boot_phase("nvs", boot_nvs_init)) {
        return;
    }

    product_app_run();
}

static bool boot_step_ok(const char *step, bool ok)
{
    ESP_LOGI("app_main", "boot_step=%s", step);
    if (!ok) {
        ESP_LOGE("app_main", "%s_failed", step);
        return false;
    }
    return true;
}

static bool boot_step_ptr(const char *step, const void *ptr)
{
    ESP_LOGI("app_main", "boot_step=%s", step);
    if (ptr == NULL) {
        ESP_LOGE("app_main", "%s_failed", step);
        return false;
    }
    return true;
}

static void boot_step_note(const char *step)
{
    ESP_LOGI("app_main", "boot_step=%s", step);
}

static void boot_init_step_label(const char *phase, char *step, size_t step_len)
{
    snprintf(step, step_len, "%s_init", phase);
}

static void boot_probe_before(const char *phase)
{
    char mem_before[BOOT_PHASE_LABEL_MAX];

    snprintf(mem_before, sizeof mem_before, "before_%s", phase);
    product_mem_probe_heap(mem_before, 0);
}

static void boot_probe_after(const char *phase, bool lvgl_pool)
{
    char mem_after[BOOT_PHASE_LABEL_MAX];

    snprintf(mem_after, sizeof mem_after, "after_%s", phase);
    if (lvgl_pool) {
        product_mem_probe(mem_after, 0);
    } else {
        product_mem_probe_heap(mem_after, 0);
    }
}

static bool boot_phase_done(const char *phase, bool lvgl_after)
{
    char ok_note[BOOT_PHASE_LABEL_MAX];

    snprintf(ok_note, sizeof ok_note, "%s_ok", phase);
    boot_step_note(ok_note);
    boot_probe_after(phase, lvgl_after);
    return true;
}

static bool boot_phase(const char *phase, bool (*init)(void))
{
    char step[BOOT_PHASE_LABEL_MAX];

    boot_probe_before(phase);
    boot_init_step_label(phase, step, sizeof step);
    if (!boot_step_ok(step, init())) {
        return false;
    }
    return boot_phase_done(phase, false);
}

static bool boot_phase_display(void)
{
    const char *phase = "display";
    char step[BOOT_PHASE_LABEL_MAX];

    boot_probe_before(phase);
    boot_init_step_label(phase, step, sizeof step);
    lv_display_t *display = product_display_start();
    if (!boot_step_ptr(step, display)) {
        return false;
    }
    return boot_phase_done(phase, true);
}

static bool boot_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            return false;
        }
        err = nvs_flash_init();
    }
    return err == ESP_OK;
}

static void boot_backlight_after_display(void)
{
    /* If mute is latched at boot, keep the display dark for the whole early boot path,
     * including when UI attach never succeeds. */
    if (product_hardware_buttons_mute_latched()) {
        (void)bsp_display_backlight_off();
        ESP_LOGI("app_main", "boot_backlight policy=dark mute_latched=1");
        return;
    }
    (void)bsp_display_brightness_set(80);
    (void)bsp_display_backlight_on();
    ESP_LOGI("app_main", "boot_backlight policy=on brightness=80 mute_latched=0");
}

static void boot_log_identity(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    uint8_t mac[6];

    ESP_LOGI("app_main", "build=%s", FIRMWARE_BUILD_ID);
    esp_chip_info(&chip);
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    ESP_LOGI("app_main", "version=%s idf=%s reset_reason=%d cores=%d revision=%d",
             app->version, app->idf_ver, (int)esp_reset_reason(), chip.cores, chip.revision);
    ESP_LOGI("app_main", "hardware=%02x:%02x:%02x:%02x:%02x:%02x free_heap=%lu",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             (unsigned long)esp_get_free_heap_size());
}
