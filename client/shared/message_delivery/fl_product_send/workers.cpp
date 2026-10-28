#include "fl_product_send_workers.hpp"

namespace fl_product_send
{

WorkerTicket begin_send_worker(fl_product_login::SessionWriter *session, int64_t now_ms)
{
    return fl_product_login::begin_send_worker(session, now_ms);
}

int deliver_send_completion(fl_product_login::SessionWriter *session, const WorkerTicket &ticket,
                            uint32_t generation, int http_status, int64_t now_ms)
{
    return fl_product_login::deliver_worker_completion(session, ticket, generation, http_status, now_ms);
}

} /* namespace fl_product_send */
