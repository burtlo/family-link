#include "product_login.hpp"
#include "product_login_service.hpp"
#include "esp_log.h"

// Compatibility symbols for compiled legacy features. The product runtime owns
// LoginService and never calls these obsolete widget/presentation entry points.
namespace product_login
{
bool attach(lv_obj_t *, lv_obj_t *)
{
    ESP_LOGE("app_main", "vc_construct name=LegacyLogin");
    return false;
}
void schedule_wifi_link(bool up, int reason) { product_ui::loginService().scheduleWifi(up, reason); }
void schedule_wifi_driver_failed() { product_ui::loginService().scheduleWifiDriverFailed(); }
void on_boot_pressed() {}
void on_mute_level(int) {}
void present(bool) {}
bool is_signed_in() { return false; }
void split_pane_release() {}
void split_pane_rebuild() {}
fl_product_login::SessionWriter *session()
{
    return const_cast<fl_product_login::SessionWriter *>(&product_ui::loginService().session());
}
} // namespace product_login
