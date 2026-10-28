#ifndef FL_PRODUCT_LOGIN_P1_WIRE_HPP
#define FL_PRODUCT_LOGIN_P1_WIRE_HPP

#include "fl_product_login_session.hpp"

namespace fl_product_login
{

/* P1 login contract 0.13: hangout roster, session login, endpoint health. */

RosterResult classify_roster_http(uint64_t operation_id, int perform_ok, int http_status,
                                  const char *body, int body_len);

RosterResult deliver_classified_roster(SessionWriter *session, uint64_t operation_id, int perform_ok,
                                       int http_status, const char *body, int body_len, int64_t now_ms);

PinResult classify_pin_http(uint64_t operation_id, uint32_t generation, const char *session_id,
                            const char *user_id, int perform_ok, int http_status, const char *body,
                            int body_len);

PinResult deliver_classified_pin(SessionWriter *session, uint64_t operation_id, uint32_t generation,
                                 const char *session_id, const char *user_id, int perform_ok,
                                 int http_status, const char *body, int body_len, int64_t now_ms);

int classify_refresh_http(int perform_ok, int http_status, const char *body, int body_len);
int classify_invalidate_http(int perform_ok, int http_status, const char *body, int body_len);
int classify_health_ack_http(int perform_ok, int http_status, const char *body, int body_len);
/* Returns 1 when server_time is present and copied; 0 when absent or unreadable. */
int parse_health_server_time(const char *body, int body_len, char *out, int out_cap);
void format_invalidate_body(uint64_t pin_operation_id, const char *session_token, char *out, int out_cap);

/* Maps the session's numeric submission id to a P1 operation_id string. */
void format_p1_operation_id(uint64_t operation_id, char *out, int out_cap);

} /* namespace fl_product_login */

#endif
