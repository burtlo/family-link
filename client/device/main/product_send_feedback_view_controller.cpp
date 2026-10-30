#include "product_send_feedback_view_controller.hpp"

#include "product_theme.hpp"

#include <cstring>

namespace product_ui
{
namespace
{
void text(lv_obj_t *label, const char *value)
{
    if (label && std::strcmp(lv_label_get_text(label), value) != 0)
        lv_label_set_text(label, value);
}
} // namespace

bool SendFeedbackViewController::createView(lv_obj_t *parent)
{
    if (!SplitPaneViewController::createView(parent))
        return false;
    title_ = lv_label_create(upperSlot());
    subtitle_ = lv_label_create(upperSlot());
    body_ = lv_label_create(lowerSlot());
    if (!title_ || !subtitle_ || !body_)
        return false;
    lv_obj_set_width(title_, 296);
    lv_obj_set_width(subtitle_, 296);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_DOT);
    lv_label_set_long_mode(subtitle_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_font(subtitle_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_font(body_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title_, lv_color_hex(product_theme::Dark::kHeadingRgb24), 0);
    lv_obj_set_style_text_color(subtitle_, lv_color_hex(product_theme::Dark::kSubheadingRgb24), 0);
    lv_obj_set_style_text_color(body_, lv_color_hex(product_theme::Dark::kTextPrimaryRgb24), 0);
    lv_obj_align(title_, LV_ALIGN_TOP_LEFT, 12, 4);
    lv_obj_align(subtitle_, LV_ALIGN_BOTTOM_LEFT, 12, -4);
    lv_obj_set_style_text_align(body_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(body_);
    render();
    return true;
}

void SendFeedbackViewController::render()
{
    if (!title_)
        return;
    char title[64];
    char subtitle[96];
    char body[64];
    delivery_.header(title, static_cast<int>(sizeof(title)), subtitle, static_cast<int>(sizeof(subtitle)));
    delivery_.body(body, static_cast<int>(sizeof(body)));
    text(title_, title);
    text(subtitle_, subtitle);
    text(body_, body);
}

void SendFeedbackViewController::onBoot()
{
    if (navigation_.leave)
        navigation_.leave(navigation_.context);
}

void SendFeedbackViewController::viewWasRemoved()
{
    title_ = subtitle_ = body_ = nullptr;
    SplitPaneViewController::viewWasRemoved();
}

} // namespace product_ui
