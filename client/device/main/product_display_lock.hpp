#pragma once

#include "bsp/esp-box-3.h"

namespace product_display
{

/** RAII wrapper for bsp_display_lock / bsp_display_unlock (LVGL is not thread-safe). */
class DisplayLock
{
  public:
    explicit DisplayLock(int timeout_ms)
        : locked_(bsp_display_lock(timeout_ms) != 0) {}
    ~DisplayLock()
    {
        if (locked_) {
            bsp_display_unlock();
        }
    }
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
