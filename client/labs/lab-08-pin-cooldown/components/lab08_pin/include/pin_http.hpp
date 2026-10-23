#ifndef LAB08_PIN_HTTP_HPP
#define LAB08_PIN_HTTP_HPP

#include "session.hpp"

namespace lab08 {

/* Classify one finished exchange. operation_id is the id captured at submission.
   This function does not read the session's current or next operation id. */
lab07::PinResult classify_pin_http(uint64_t operation_id, uint32_t generation, const char *session_id,
                                   const char *user_id, int perform_ok, int http_status, const char *body,
                                   int body_len);

lab07::PinResult deliver_classified_pin(lab07::SessionWriter *session, uint64_t operation_id,
                                        uint32_t generation, const char *session_id, const char *user_id,
                                        int perform_ok, int http_status, const char *body, int body_len,
                                        int64_t now_ms);

} /* namespace lab08 */

#endif
