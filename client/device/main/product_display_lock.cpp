#include "product_display_lock.hpp"

#include "bsp/esp-box-3.h"

namespace product_display
{

DisplayLock::DisplayLock(int timeout_ms)
    : locked_(bsp_display_lock(timeout_ms) != 0)
{
}

DisplayLock::~DisplayLock()
{
    if (locked_) {
        bsp_display_unlock();
    }
}

} /* namespace product_display */
