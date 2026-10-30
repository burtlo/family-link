#include "product_stage_router.hpp"

#include "product_inbox.hpp"
#include "product_login.hpp"
#include "product_mem.hpp"
#include "product_record.hpp"
#include "product_send.hpp"
#include "product_split_pane.hpp"
#include "product_volume.hpp"

#include "esp_log.h"
#include "lvgl.h"

#include "fl_product_login_session.hpp"

namespace product_stage
{

namespace
{

const char *kTag = "product_stage";

int apply_send_stage(const product_send::StageResult *result)
{
    if (result == 0 || result->ignored) {
        return 0;
    }
    if (result->opened_volume) {
        return 0;
    }
    stage_shell::splitPane().setStage((fl_shell_stage_t)result->shell_stage);
    ESP_LOGI(kTag, "place=%d shell=%d", result->place, result->shell_stage);
    product_mem::probe("stage", result->place);
    return 1;
}

} /* namespace */

Screen screen()
{
    if (!product_login::is_signed_in()) {
        return Screen::Login;
    }
    switch (stage_shell::splitPane().stage()) {
        case FL_SHELL_STAGE_VOLUME:
            return Screen::Volume;
        case FL_SHELL_STAGE_RECORD:
            return Screen::Record;
        case FL_SHELL_STAGE_PICKER:
        case FL_SHELL_STAGE_RECEIPT:
            return Screen::Send;
        default:
            return Screen::Inbox;
    }
}

/* Sole writer of module visibility. Each module hides or shows only its own
 * widgets; LVGL skips hidden subtrees for both drawing and hit-testing, so
 * no module reorders siblings to win touch. */
void present()
{
    static Screen s_shown = Screen::Count;
    const Screen active = screen();
    if (active != s_shown) {
        ESP_LOGI(kTag, "screen=%d from=%d", static_cast<int>(active), static_cast<int>(s_shown));
        s_shown = active;
    }
    product_login::present(active == Screen::Login);
    product_inbox::set_ui_visible(active == Screen::Inbox);
    product_volume::set_visible(active == Screen::Volume);
    product_send::present(active == Screen::Send);
    product_record::set_visible(active == Screen::Record);
}

void on_signed_in()
{
    fl_product_login::SessionWriter *session = product_login::session();
    const char *user = session != nullptr ? session->signed_user() : "";
    product_send::note_inbox();
    product_send::on_signed_in(user);
    stage_shell::splitPane().setStage(FL_SHELL_STAGE_INBOX);
    ESP_LOGI(kTag, "stage=inbox");
}

int on_red_record()
{
    product_send::StageResult result;
    if (product_volume::active()) {
        return 0;
    }
    result = product_send::apply_intent(product_send::Intent::Red, product_login::is_signed_in());
    return apply_send_stage(&result);
}

int on_boot()
{
    product_send::StageResult result;
    if (product_volume::active()) {
        return 0;
    }
    result = product_send::apply_intent(product_send::Intent::Boot, product_login::is_signed_in());
    if (result.opened_volume) {
        return product_inbox::on_boot_pressed();
    }
    return apply_send_stage(&result);
}

void on_mute()
{
    (void)product_send::apply_intent(product_send::Intent::Mute, product_login::is_signed_in());
}

int on_wipe_complete()
{
    product_send::StageResult result;
    result = product_send::apply_intent(product_send::Intent::WipeComplete, true);
    return apply_send_stage(&result);
}

int on_capture_failed()
{
    product_send::StageResult result;
    result = product_send::apply_intent(product_send::Intent::CaptureFailed, product_login::is_signed_in());
    return apply_send_stage(&result);
}

int on_take_finished()
{
    product_send::StageResult result;
    result = product_send::apply_intent(product_send::Intent::TakeFinished, product_login::is_signed_in());
    return apply_send_stage(&result);
}

} /* namespace product_stage */
