#include "product_stage_router.h"

#include "product_inbox_fw.h"
#include "product_record_fw.h"
#include "product_shell_fw.h"
#include "product_volume_fw.h"

#include "esp_log.h"
#include "lvgl.h"

static const char *kTag = "product_stage";

extern "C" void product_stage_on_signed_in(void)
{
    product_shell_set_stage(FL_SHELL_STAGE_INBOX);
    product_inbox_set_ui_visible(1);
    ESP_LOGI(kTag, "stage=inbox");
}

extern "C" int product_stage_on_red_record(void)
{
    if (product_volume_active()) {
        return 0;
    }
    if (!product_record_attach(lv_screen_active())) {
        ESP_LOGW(kTag, "record_attach_failed");
        return 0;
    }
    product_shell_set_stage(FL_SHELL_STAGE_RECORD);
    ESP_LOGI(kTag, "stage=record");
    return 1;
}
