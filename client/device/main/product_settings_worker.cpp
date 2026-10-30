#include "product_settings_service.hpp"

#ifdef ESP_PLATFORM
#include "bsp/esp-box-3.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nvs.h"

namespace product_ui
{
namespace settings_worker
{
namespace
{
QueueHandle_t jobs;
QueueHandle_t results;
TaskHandle_t worker;
esp_codec_dev_handle_t speaker;

bool load(int &stop)
{
    nvs_handle_t handle;
    if (nvs_open("fl_desk", NVS_READONLY, &handle) != ESP_OK) return false;
    uint8_t value = 12;
    const esp_err_t error = nvs_get_u8(handle, "vol_stop", &value);
    nvs_close(handle);
    if (error != ESP_OK || value >= fl_product_volume::kVolumeStopCount) return false;
    stop = value;
    return true;
}
bool save(int stop)
{
    nvs_handle_t handle;
    if (nvs_open("fl_desk", NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t error = nvs_set_u8(handle, "vol_stop", static_cast<uint8_t>(stop));
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error == ESP_OK;
}
bool apply(int stop)
{
    if (!speaker) speaker = bsp_audio_codec_speaker_init();
    if (!speaker) return false;
    const int codec = fl_product_volume::volume_codec_for_stop_index(stop);
    const int volume_result = esp_codec_dev_set_out_vol(speaker, codec);
    const int mute_result = esp_codec_dev_set_out_mute(speaker, codec == 0);
    return volume_result == ESP_CODEC_DEV_OK && mute_result == ESP_CODEC_DEV_OK;
}
void run(void *)
{
    SettingsJob job;
    while (true) {
        if (xQueueReceive(jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        SettingsResult result;
        result.operation = job.operation;
        result.kind = job.kind;
        result.stop = job.stop;
        if (job.kind == SettingsJobKind::Load) result.success = load(result.stop);
        else if (job.kind == SettingsJobKind::Save) result.success = save(job.stop);
        else result.success = apply(job.stop);
        ESP_LOGI("product_settings", "operation=%lu kind=%d success=%d",
                 static_cast<unsigned long>(job.operation), static_cast<int>(job.kind), result.success);
        // The worker lives for the application lifetime. It holds value jobs,
        // never a SettingsService pointer; controller destruction cannot stop it.
        if (xQueueSend(results, &result, portMAX_DELAY) != pdTRUE)
            ESP_LOGE("product_settings", "result_queue_failed operation=%lu",
                     static_cast<unsigned long>(job.operation));
    }
}
}
bool start()
{
    if (worker) return true;
    jobs = xQueueCreate(1, sizeof(SettingsJob));
    results = xQueueCreate(1, sizeof(SettingsResult));
    if (!jobs || !results || xTaskCreate(run, "settings", 4096, nullptr, 3, &worker) != pdPASS) {
        if (jobs) vQueueDelete(jobs);
        if (results) vQueueDelete(results);
        jobs = results = nullptr;
        worker = nullptr;
        ESP_LOGE("product_settings", "worker_start_failed");
        return false;
    }
    return true;
}
bool submit(const SettingsJob &job)
{
    return jobs && xQueueSend(jobs, &job, 0) == pdTRUE;
}
bool poll(SettingsResult &result)
{
    return results && xQueueReceive(results, &result, 0) == pdTRUE;
}
} // namespace settings_worker
} // namespace product_ui
#endif
