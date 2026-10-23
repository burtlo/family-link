#include "product_stage_router.h"

#include "product_inbox_fw.h"
#include "product_login_fw.h"
#include "product_record_fw.h"
#include "product_send_fw.h"
#include "product_shell_fw.h"
#include "product_volume_fw.h"

#include "esp_log.h"
#include "lvgl.h"

static const char *kTag = "product_stage";

static int apply_send_stage(const product_send_stage_result_t *result)
{
    if (result == 0 || result->ignored) {
        return 0;
    }
    if (result->opened_volume) {
        return 0;
    }
    if (result->show_record_canvas) {
        (void)product_record_ensure(lv_screen_active());
    }
    product_record_set_visible(result->show_record_canvas);
    product_shell_set_stage((fl_shell_stage_t)result->shell_stage);
    product_inbox_set_ui_visible(result->show_split_pane);
    product_send_sync_stage();
    ESP_LOGI(kTag, "place=%d shell=%d", result->place, result->shell_stage);
    return result->ignored ? 0 : 1;
}

extern "C" void product_stage_on_signed_in(void)
{
    fl_product_login::SessionWriter *session = product_login_session();
    const char *user = session != nullptr ? session->signed_user() : "";
    product_send_note_inbox();
    product_send_on_signed_in(user);
    product_shell_set_stage(FL_SHELL_STAGE_INBOX);
    product_inbox_set_ui_visible(1);
    product_record_set_visible(0);
    ESP_LOGI(kTag, "stage=inbox");
}

extern "C" int product_stage_on_red_record(void)
{
    product_send_stage_result_t result;
    if (product_volume_active()) {
        return 0;
    }
    result = product_send_apply_intent(PRODUCT_SEND_INTENT_RED, product_login_is_signed_in());
    return apply_send_stage(&result);
}

extern "C" int product_stage_on_boot(void)
{
    product_send_stage_result_t result;
    if (product_volume_active()) {
        return 0;
    }
    result = product_send_apply_intent(PRODUCT_SEND_INTENT_BOOT, product_login_is_signed_in());
    if (result.opened_volume) {
        return product_inbox_on_boot_pressed();
    }
    return apply_send_stage(&result);
}

extern "C" void product_stage_on_mute(void)
{
    (void)product_send_apply_intent(PRODUCT_SEND_INTENT_MUTE, product_login_is_signed_in());
    product_record_set_visible(0);
}

extern "C" int product_stage_on_wipe_complete(void)
{
    product_send_stage_result_t result;
    result = product_send_apply_intent(PRODUCT_SEND_INTENT_WIPE_COMPLETE, 1);
    return apply_send_stage(&result);
}

extern "C" int product_stage_on_capture_failed(void)
{
    product_send_stage_result_t result;
    result = product_send_apply_intent(PRODUCT_SEND_INTENT_CAPTURE_FAILED, product_login_is_signed_in());
    return apply_send_stage(&result);
}

extern "C" int product_stage_on_take_finished(void)
{
    product_send_stage_result_t result;
    result = product_send_apply_intent(PRODUCT_SEND_INTENT_TAKE_FINISHED, product_login_is_signed_in());
    return apply_send_stage(&result);
}
