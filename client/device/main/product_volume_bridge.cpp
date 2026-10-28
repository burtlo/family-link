#include "product_volume_fw.h"

#include "product_inbox_fw.h"
#include "product_shell_fw.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "fl_product_inbox_carousel.hpp"
#include "fl_product_login_session.hpp"
#include "fl_product_volume_journey.hpp"
#include "fl_product_volume_stops.hpp"

namespace {

constexpr const char *kTag = "product_volume";
constexpr const char *kNvsNamespace = "fl_desk";
constexpr const char *kNvsStopKey = "vol_stop";

fl_product_volume::VolumeStorage s_storage;
fl_product_volume::DeskVolumeJourney s_journey;
fl_product_inbox::CarouselSession *s_inbox = nullptr;
fl_product_login::SessionWriter *s_login = nullptr;

lv_obj_t *s_upper_root = nullptr;
lv_obj_t *s_lower_root = nullptr;
lv_obj_t *s_title_label = nullptr;
lv_obj_t *s_slider = nullptr;
bool s_attached = false;

int load_stop_from_nvs(int *index_out)
{
    nvs_handle_t handle;
    if (index_out == nullptr) {
        return -1;
    }
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return -1;
    }
    uint8_t value = (uint8_t)fl_product_volume::volume_stop_index_default();
    if (nvs_get_u8(handle, kNvsStopKey, &value) != ESP_OK) {
        nvs_close(handle);
        return -1;
    }
    nvs_close(handle);
    *index_out = fl_product_volume::volume_stop_index_clamp((int)value);
    return 0;
}

int persist_stop_to_nvs(int index)
{
    nvs_handle_t handle;
    const uint8_t value = (uint8_t)fl_product_volume::volume_stop_index_clamp(index);
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return -1;
    }
    if (nvs_set_u8(handle, kNvsStopKey, value) != ESP_OK) {
        nvs_close(handle);
        return -1;
    }
    if (nvs_commit(handle) != ESP_OK) {
        nvs_close(handle);
        return -1;
    }
    nvs_close(handle);
    return 0;
}

void sync_storage_from_nvs()
{
    int index = fl_product_volume::volume_stop_index_default();
    if (load_stop_from_nvs(&index) == 0) {
        s_storage.reset_for_tests();
        (void)s_storage.persist_stop_index(index);
    }
}

void render_volume()
{
    if (s_slider == nullptr) {
        return;
    }
    lv_slider_set_value(s_slider, s_journey.stop_index(), LV_ANIM_OFF);
}

void on_speaker_codec(int codec) { (void)codec; }

void on_slider_changed(lv_event_t *event)
{
    lv_obj_t *slider;
    int index;
    (void)event;
    slider = s_slider;
    if (slider == nullptr) {
        return;
    }
    index = (int)lv_slider_get_value(slider);
    s_journey.set_stop_index(index);
}

} /* namespace */

void product_volume_bind_models(fl_product_inbox::CarouselSession *inbox,
                                fl_product_login::SessionWriter *login)
{
    if (inbox != nullptr) {
        s_inbox = inbox;
    }
    if (login != nullptr) {
        s_login = login;
    }
}

extern "C" bool product_volume_attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot)
{
    if (upper_slot == nullptr || lower_slot == nullptr || s_attached) {
        return false;
    }
    sync_storage_from_nvs();
    s_journey.bind_storage(&s_storage);
    s_journey.set_speaker_codec_callback(on_speaker_codec);
    s_journey.on_power_on();
    if (!product_volume_build_widgets(upper_slot, lower_slot)) {
        return false;
    }
    s_attached = true;
    ESP_LOGI(kTag, "V2_PRODUCT_VOLUME_READY build=%s", FIRMWARE_BUILD_ID);
    return true;
}

extern "C" void product_volume_forget_widgets(void)
{
    s_upper_root = nullptr;
    s_lower_root = nullptr;
    s_title_label = nullptr;
    s_slider = nullptr;
}

extern "C" bool product_volume_build_widgets(lv_obj_t *upper_slot, lv_obj_t *lower_slot)
{
    if (upper_slot == nullptr || lower_slot == nullptr) {
        return false;
    }
    s_upper_root = lv_obj_create(upper_slot);
    s_lower_root = lv_obj_create(lower_slot);
    s_title_label = lv_label_create(s_upper_root);
    s_slider = lv_slider_create(s_lower_root);
    if (s_upper_root == nullptr || s_lower_root == nullptr || s_title_label == nullptr ||
        s_slider == nullptr) {
        return false;
    }
    lv_obj_remove_style_all(s_upper_root);
    lv_obj_remove_style_all(s_lower_root);
    lv_obj_set_size(s_upper_root, 320, 80);
    lv_obj_set_size(s_lower_root, 320, 160);
    lv_obj_set_style_bg_opa(s_upper_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(s_lower_root, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_upper_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lower_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_upper_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_lower_root, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(s_title_label, "Volume");
    lv_obj_align(s_title_label, LV_ALIGN_LEFT_MID, 8, 0);
    lv_slider_set_range(s_slider, 0, fl_product_volume::kVolumeStopCount - 1);
    lv_obj_set_width(s_slider, 280);
    lv_obj_center(s_slider);
    lv_obj_add_event_cb(s_slider, on_slider_changed, LV_EVENT_VALUE_CHANGED, nullptr);
    render_volume();
    return true;
}

extern "C" void product_volume_service(int64_t now_ms)
{
    (void)now_ms;
    if (!s_attached || !product_volume_active()) {
        return;
    }
    render_volume();
}

extern "C" int product_volume_active(void)
{
    return s_journey.screen() == fl_product_volume::kVolumeScreenVolume ? 1 : 0;
}

extern "C" int product_volume_on_boot_from_inbox(void)
{
    int64_t now_ms;
    if (s_inbox == nullptr || s_login == nullptr || s_login->signed_in() == 0 ||
        product_volume_active()) {
        return 0;
    }
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    if (s_journey.enter_from_inbox(*s_inbox, now_ms) != 1) {
        return 0;
    }
    render_volume();
    product_shell_set_stage(FL_SHELL_STAGE_VOLUME);
    return 1;
}

extern "C" int product_volume_on_boot_leave(void)
{
    int64_t now_ms;
    int index;
    if (s_inbox == nullptr || !product_volume_active()) {
        return 0;
    }
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    index = s_journey.stop_index();
    if (s_journey.leave_to_inbox(*s_inbox, now_ms) != 1) {
        return 0;
    }
    if (s_storage.persist_stop_index(index) == 0) {
        (void)persist_stop_to_nvs(index);
    }
    product_shell_set_stage(FL_SHELL_STAGE_INBOX);
    return 1;
}

extern "C" int product_volume_on_mute(void)
{
    int64_t now_ms;
    int index;
    if (s_inbox == nullptr || s_login == nullptr || !product_volume_active()) {
        return 0;
    }
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    index = s_journey.stop_index();
    if (s_journey.mute_from_volume(*s_inbox, *s_login, now_ms) != 1) {
        return 0;
    }
    if (s_storage.persist_stop_index(index) == 0) {
        (void)persist_stop_to_nvs(index);
    }
    return 1;
}

extern "C" int product_volume_on_red_record(void) { return s_journey.on_red_record(); }

extern "C" void product_volume_on_wifi(bool up)
{
    if (!up) {
        s_journey.note_connectivity_lost();
    }
}

extern "C" void product_volume_set_visible(int visible)
{
    if (!s_attached || s_upper_root == nullptr || s_lower_root == nullptr) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(s_upper_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_lower_root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_upper_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lower_root, LV_OBJ_FLAG_HIDDEN);
    }
}
