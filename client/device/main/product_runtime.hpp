#pragma once

#include "product_login_view_controller.hpp"

namespace product_ui
{

struct RuntimePlatform {
    int (*muteLatched)() = nullptr;
    void (*blankDisplay)() = nullptr;
    void (*wakeDisplay)() = nullptr;
    bool (*takeBoot)() = nullptr;
    bool (*takeRed)() = nullptr;
};

class ProductRuntime
{
  public:
    ProductRuntime(RootViewController &root, LoginService &service, RuntimePlatform platform = {});
    ~ProductRuntime();
    bool start();
    void tick(int64_t now);
    LoginNavigation navigation();
    bool muted() const { return muted_; }

  private:
    static void on_product_timer(lv_timer_t *timer);
    static void requestNavigation(void *context, LoginDestination destination, const char *user);
    void pollInput(int64_t now);
    void serviceFeatures();
    void present();
    RootViewController &root_;
    LoginService &service_;
    RuntimePlatform platform_;
    lv_timer_t *timer_ = nullptr;
    bool muted_ = false;
    bool input_known_ = false;
    bool navigation_pending_ = false;
    char selected_user_[fl_product_login::kMaxId]{};
};

} // namespace product_ui
