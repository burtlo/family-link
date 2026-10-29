#pragma once

namespace product_display
{

/** RAII wrapper for bsp_display_lock / bsp_display_unlock (LVGL is not thread-safe). */
class DisplayLock
{
  public:
    explicit DisplayLock(int timeout_ms);
    ~DisplayLock();
    explicit operator bool() const
    {
        return locked_;
    }

    DisplayLock(const DisplayLock &) = delete;
    DisplayLock &operator=(const DisplayLock &) = delete;

  private:
    bool locked_;
};

} /* namespace product_display */
