#pragma once

#include <cstdint>

namespace fl_product_login
{
class SessionWriter;
}

namespace product_login_http
{

bool worker_start(fl_product_login::SessionWriter *session);
void worker_stop();
/* Drop queued roster work and clear the in-flight roster fence (mute / unmute). */
void on_mute();
/* UI-thread tick: queue work for the worker and apply finished HTTP results. */
void service(fl_product_login::SessionWriter *session, int64_t now_ms);

} /* namespace product_login_http */
