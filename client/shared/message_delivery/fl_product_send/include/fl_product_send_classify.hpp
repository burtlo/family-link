#ifndef FL_PRODUCT_SEND_CLASSIFY_HPP
#define FL_PRODUCT_SEND_CLASSIFY_HPP

#include "fl_product_send_types.hpp"

namespace fl_product_send
{

Com013Class classify_http_result(int wifi_up, int transport_ok, int http_status, const char *body,
                                 int body_len, int refresh_failed);

const char *ui_phase_label(SendUiPhase phase);
const char *connectivity_wait_label(int wifi_up, int refresh_failed);

} /* namespace fl_product_send */

#endif
