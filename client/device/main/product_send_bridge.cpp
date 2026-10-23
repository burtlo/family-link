#include "product_send_fw.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "fl_product_send_classify.hpp"
#include "fl_product_send_journey.hpp"

namespace {

constexpr const char *kTag = "product_send";
fl_product_send::SendJourney s_journey;
lv_obj_t *s_upper_label = nullptr;
lv_obj_t *s_lower_label = nullptr;
lv_timer_t *s_timer = nullptr;

void render_send()
{
    const char *phase = fl_product_send::ui_phase_label(s_journey.ui_phase());
    if (s_upper_label != nullptr) {
        lv_label_set_text(s_upper_label, phase[0] != '\0' ? phase : "Send");
    }
    if (s_lower_label != nullptr) {
        lv_label_set_text(s_lower_label, s_journey.restored_inbox_selection());
    }
}

void on_send_timer(lv_timer_t *timer)
{
    int64_t now_ms;
    (void)timer;
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    s_journey.advance_to(now_ms);
    render_send();
}

} /* namespace */

extern "C" bool product_send_attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot)
{
    if (upper_slot == nullptr || lower_slot == nullptr) {
        return false;
    }
    s_journey.reset();
    s_upper_label = lv_label_create(upper_slot);
    s_lower_label = lv_label_create(lower_slot);
    if (s_upper_label == nullptr || s_lower_label == nullptr) {
        return false;
    }
    lv_obj_center(s_upper_label);
    lv_obj_center(s_lower_label);
    render_send();
    s_timer = lv_timer_create(on_send_timer, 50, nullptr);
    ESP_LOGI(kTag, "V2_PRODUCT_SEND_READY");
    return s_timer != nullptr;
}

extern "C" void product_send_detach(void)
{
    if (s_timer != nullptr) {
        lv_timer_del(s_timer);
        s_timer = nullptr;
    }
    s_upper_label = nullptr;
    s_lower_label = nullptr;
}
