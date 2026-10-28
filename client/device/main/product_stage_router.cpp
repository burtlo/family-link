#include "product_stage_router.h"

#include "product_inbox_fw.h"
#include "product_login_fw.h"
#include "product_mem_probe.h"
#include "product_record_fw.h"
#include "product_send_fw.h"
#include "product_split_pane.hpp"
#include "product_ui_window.h"
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
        (void)product_ui_window_show_record();
    }
    stage_shell::splitPane().setStage((fl_shell_stage_t)result->shell_stage);
    ESP_LOGI(kTag, "place=%d shell=%d", result->place, result->shell_stage);
    product_mem_probe("stage", result->place);
    return 1;
}

extern "C" product_screen_t product_stage_screen(void)
{
    if (!product_login_is_signed_in()) {
        return PRODUCT_SCREEN_LOGIN;
    }
    switch (stage_shell::splitPane().stage()) {
    case FL_SHELL_STAGE_VOLUME:
        return PRODUCT_SCREEN_VOLUME;
    case FL_SHELL_STAGE_RECORD:
        return PRODUCT_SCREEN_RECORD;
    case FL_SHELL_STAGE_PICKER:
    case FL_SHELL_STAGE_RECEIPT:
        return PRODUCT_SCREEN_SEND;
    default:
        return PRODUCT_SCREEN_INBOX;
    }
}

/* Sole writer of module visibility. Each module hides or shows only its own
 * widgets; LVGL skips hidden subtrees for both drawing and hit-testing, so
 * no module reorders siblings to win touch. */
extern "C" void product_stage_present(void)
{
    static product_screen_t s_shown = PRODUCT_SCREEN_COUNT;
    const product_screen_t screen = product_stage_screen();
    if (screen != s_shown) {
        ESP_LOGI(kTag, "screen=%d from=%d", (int)screen, (int)s_shown);
        s_shown = screen;
    }
    if (screen == PRODUCT_SCREEN_RECORD) {
        (void)product_ui_window_show_record();
    } else {
        (void)product_ui_window_show_desk();
    }
    product_login_present(screen == PRODUCT_SCREEN_LOGIN);
    product_inbox_set_ui_visible(screen == PRODUCT_SCREEN_INBOX);
    product_volume_set_visible(screen == PRODUCT_SCREEN_VOLUME);
    product_send_present(screen == PRODUCT_SCREEN_SEND);
    product_record_set_visible(screen == PRODUCT_SCREEN_RECORD);
}

extern "C" void product_stage_on_signed_in(void)
{
    fl_product_login::SessionWriter *session = product_login_session();
    const char *user = session != nullptr ? session->signed_user() : "";
    product_send_note_inbox();
    product_send_on_signed_in(user);
    stage_shell::splitPane().setStage(FL_SHELL_STAGE_INBOX);
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
