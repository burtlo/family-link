#ifndef FL_PRODUCT_LOGIN_WORKERS_HPP
#define FL_PRODUCT_LOGIN_WORKERS_HPP

#include "fl_product_login_session.hpp"

namespace fl_product_login
{

/* P6 stubs: capture generation at submit; drop mismatched completions. */

struct WorkerTicket {
    uint64_t operation_id;
    uint32_t generation;
    char user_id[kMaxId];
    char kind[16];
};

WorkerTicket begin_inbox_worker(SessionWriter *session, int64_t now_ms);
WorkerTicket begin_send_worker(SessionWriter *session, int64_t now_ms);
int deliver_worker_completion(SessionWriter *session, const WorkerTicket &ticket, uint32_t generation,
                              int http_status, int64_t now_ms);

} /* namespace fl_product_login */

#endif
