#ifndef LAB07_ROSTER_HTTP_HPP
#define LAB07_ROSTER_HTTP_HPP

#include "session.hpp"

namespace lab07 {

/* Classify one finished HTTP exchange into a roster completion.
   operation_id is the id captured when the request was submitted.
   This does not read the session's current or next operation id. */
RosterResult classify_roster_http(uint64_t operation_id, int perform_ok, int http_status,
                                  const char *body, int body_len);

/* Classify, then deliver that completion through SessionWriter.
   The returned result is the classified completion, including its submission id. */
RosterResult deliver_classified_roster(SessionWriter *session, uint64_t operation_id, int perform_ok,
                                       int http_status, const char *body, int body_len, int64_t now_ms);

} /* namespace lab07 */

#endif
