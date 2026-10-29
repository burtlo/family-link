#include "product_app_idle.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace product_app
{

/* app_main's task must not return after boot: LVGL, network, and workers run elsewhere.
 * Sleep here so the process stays up and serial logs remain useful without aborting boot. */
void idle_forever()
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

} /* namespace product_app */
