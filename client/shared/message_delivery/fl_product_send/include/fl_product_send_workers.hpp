#ifndef FL_PRODUCT_SEND_WORKERS_HPP
#define FL_PRODUCT_SEND_WORKERS_HPP

#include "fl_product_login_workers.hpp"

namespace fl_product_send {

using WorkerTicket = fl_product_login::WorkerTicket;

WorkerTicket begin_send_worker(fl_product_login::SessionWriter *session, int64_t now_ms);
int deliver_send_completion(fl_product_login::SessionWriter *session, const WorkerTicket &ticket,
                            uint32_t generation, int http_status, int64_t now_ms);

} /* namespace fl_product_send */

#endif
