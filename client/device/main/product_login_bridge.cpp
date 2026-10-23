#include "product_login_fw.h"
#include "product_login_http_worker.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "fl_product_login_session.hpp"

#include <stdio.h>

namespace {

fl_product_login::SessionWriter s_session;
lv_obj_t *s_upper_label = nullptr;
lv_obj_t *s_lower_label = nullptr;
lv_timer_t *s_login_timer = nullptr;
bool s_wifi_up = false;

void render_login_copy()
{
    static char cooldown_line[48];
    const char *upper = "Loading hangout ...";
    const char *lower = "connecting";
    if (!s_wifi_up) {
        lower = "no Wi-Fi";
    } else if (s_session.pin_phase() == fl_product_login::kPinPhaseCooldown) {
        upper = "Sign in as ...";
        snprintf(cooldown_line, sizeof(cooldown_line), "%d",
                 s_session.cooldown_remaining_ms() / 1000);
        lower = cooldown_line;
    } else if (s_session.stage() == fl_product_login::kStageRoster) {
        upper = "Sign in as ...";
        lower = "Swipe to choose a person";
    } else if (s_session.stage() == fl_product_login::kStageEmptyHangout) {
        upper = "Sign in as ...";
        lower = "No people in this hangout";
    } else if (s_session.stage() == fl_product_login::kStageConnecting) {
        lower = "connecting";
    }
    if (s_upper_label != nullptr) {
        lv_label_set_text(s_upper_label, upper);
    }
    if (s_lower_label != nullptr) {
        lv_label_set_text(s_lower_label, lower);
    }
}

void on_login_timer(lv_timer_t *timer)
{
    int64_t now_ms;
    (void)timer;
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    s_session.advance_to(now_ms);
    s_session.drain();
    product_login_http_service(&s_session, now_ms);
    render_login_copy();
}

} /* namespace */

extern "C" bool product_login_attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot)
{
    if (upper_slot == nullptr || lower_slot == nullptr) {
        return false;
    }
    s_session.boot(0, false);
    s_upper_label = lv_label_create(upper_slot);
    s_lower_label = lv_label_create(lower_slot);
    if (s_upper_label == nullptr || s_lower_label == nullptr) {
        return false;
    }
    lv_obj_center(s_upper_label);
    lv_obj_center(s_lower_label);
    render_login_copy();
    s_login_timer = lv_timer_create(on_login_timer, 50, nullptr);
    if (s_login_timer == nullptr) {
        return false;
    }
    if (!product_login_http_worker_start(&s_session)) {
        ESP_LOGW("product_login", "HTTP worker not started");
    }
    ESP_LOGI("product_login", "V2_PRODUCT_LOGIN_READY build=%s", FAMILY_BUILD_ID);
    return true;
}

extern "C" void product_login_set_wifi(bool up)
{
    s_wifi_up = up;
    s_session.set_wifi(up, (int64_t)(esp_timer_get_time() / 1000LL));
    render_login_copy();
}
