#include "product_inbox_fw.h"
#include "product_volume_fw.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "fl_product_inbox_carousel.hpp"

namespace {

constexpr const char *kTag = "product_inbox";
fl_product_inbox::CarouselSession s_inbox;
lv_obj_t *s_upper_label = nullptr;
lv_obj_t *s_lower_label = nullptr;
lv_obj_t *s_transport_label = nullptr;
lv_timer_t *s_timer = nullptr;
bool s_attached = false;

void render_inbox()
{
    const char *upper = "Messages";
    const char *lower = "";
    const char *transport = "";
    if (s_upper_label == nullptr) {
        return;
    }
    if (s_inbox.connectivity_slot() == fl_product_inbox::kSlotNoWifi) {
        lower = "no Wi-Fi";
    } else if (s_inbox.connectivity_slot() == fl_product_inbox::kSlotConnecting) {
        lower = "connecting";
    } else if (s_inbox.message_count() == 0) {
        lower = "No messages yet";
    } else {
        lower = s_inbox.selected_message_id();
    }
    if (s_inbox.playback_phase() == fl_product_inbox::kPlaybackPlaying) {
        transport = "Pause";
    } else if (!s_inbox.play_disabled()) {
        transport = "Play";
    } else {
        transport = "";
    }
    lv_label_set_text(s_upper_label, upper);
    if (s_lower_label != nullptr) {
        lv_label_set_text(s_lower_label, lower);
    }
    if (s_transport_label != nullptr) {
        lv_label_set_text(s_transport_label, transport);
    }
}

void on_inbox_timer(lv_timer_t *timer)
{
    int64_t now_ms;
    (void)timer;
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    s_inbox.advance_to(now_ms);
    render_inbox();
}

} /* namespace */

extern "C" bool product_inbox_attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot)
{
    if (upper_slot == nullptr || lower_slot == nullptr) {
        return false;
    }
    s_inbox.boot(0);
    s_upper_label = lv_label_create(upper_slot);
    s_transport_label = lv_label_create(upper_slot);
    s_lower_label = lv_label_create(lower_slot);
    if (s_upper_label == nullptr || s_lower_label == nullptr || s_transport_label == nullptr) {
        return false;
    }
    lv_obj_align(s_upper_label, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_align(s_transport_label, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_center(s_lower_label);
    lv_obj_set_style_text_color(s_upper_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(s_lower_label, lv_color_hex(0xB7CBD7), 0);
    lv_obj_set_style_text_color(s_transport_label, lv_color_hex(0xD4E6D2), 0);
    lv_obj_set_style_text_font(s_upper_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_font(s_lower_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_font(s_transport_label, &lv_font_montserrat_16, 0);
    lv_obj_add_flag(s_upper_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lower_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_transport_label, LV_OBJ_FLAG_HIDDEN);
    render_inbox();
    s_timer = lv_timer_create(on_inbox_timer, 50, nullptr);
    s_attached = s_timer != nullptr;
    if (s_attached) {
        ESP_LOGI(kTag, "V2_PRODUCT_INBOX_READY build=%s", FAMILY_BUILD_ID);
    }
    return s_attached;
}

extern "C" void product_inbox_service(int64_t now_ms)
{
    if (!s_attached) {
        return;
    }
    s_inbox.advance_to(now_ms);
    render_inbox();
}

extern "C" void product_inbox_on_signed_in(void)
{
    if (!s_attached) {
        return;
    }
    s_inbox.boot((int64_t)(esp_timer_get_time() / 1000LL));
    render_inbox();
}

extern "C" void product_inbox_on_mute(void)
{
    if (!s_attached) {
        return;
    }
    s_inbox.on_mute((int64_t)(esp_timer_get_time() / 1000LL));
    render_inbox();
}

extern "C" void product_inbox_set_wifi(bool up)
{
    if (!s_attached) {
        return;
    }
    s_inbox.set_wifi(up, (int64_t)(esp_timer_get_time() / 1000LL));
    product_volume_on_wifi(up);
    render_inbox();
}

extern "C" void product_inbox_set_ui_visible(int visible)
{
    if (!s_attached) {
        return;
    }
    if (s_upper_label != nullptr) {
        if (visible) {
            lv_obj_remove_flag(s_upper_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_upper_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_transport_label != nullptr) {
        if (visible) {
            lv_obj_remove_flag(s_transport_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_transport_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_lower_label != nullptr) {
        if (visible) {
            lv_obj_remove_flag(s_lower_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_lower_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

fl_product_inbox::CarouselSession *product_inbox_session(void) { return &s_inbox; }

extern "C" int product_inbox_on_boot_pressed(void)
{
    if (!s_attached) {
        return 0;
    }
    if (product_volume_active()) {
        return product_volume_on_boot_leave();
    }
    return product_volume_on_boot_from_inbox();
}
