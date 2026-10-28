#ifndef PRODUCT_LOGIN_HTTP_WORKER_H
#define PRODUCT_LOGIN_HTTP_WORKER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
namespace fl_product_login
{
class SessionWriter;
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

bool product_login_http_worker_start(fl_product_login::SessionWriter *session);
void product_login_http_worker_stop(void);
/* Drop queued roster work and clear the in-flight roster fence (mute / unmute). */
void product_login_http_on_mute(void);
/* UI-thread tick: queue work for the worker and apply finished HTTP results. */
void product_login_http_service(fl_product_login::SessionWriter *session, int64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
