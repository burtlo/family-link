#include "fl_product_inbox_workers.hpp"

#include "fl_product_login_session.hpp"

#include <string.h>

namespace fl_product_inbox
{

fl_product_login::WorkerTicket begin_inbox_worker(fl_product_login::SessionWriter *session,
                                                  int64_t now_ms)
{
    return fl_product_login::begin_inbox_worker(session, now_ms);
}

fl_product_login::WorkerTicket begin_media_worker(fl_product_login::SessionWriter *session,
                                                  int64_t now_ms)
{
    fl_product_login::WorkerTicket worker;
    memset(&worker, 0, sizeof(worker));
    if (session == 0) {
        return worker;
    }
    fl_product_login::ScopedTicket scoped = session->begin_scoped_operation("media", now_ms);
    worker.operation_id = scoped.operation_id;
    worker.generation = scoped.generation;
    strncpy(worker.user_id, scoped.user_id, sizeof(worker.user_id) - 1);
    strncpy(worker.kind, scoped.kind, sizeof(worker.kind) - 1);
    return worker;
}

fl_product_login::WorkerTicket begin_playback_worker(fl_product_login::SessionWriter *session,
                                                     int64_t now_ms)
{
    fl_product_login::WorkerTicket worker;
    memset(&worker, 0, sizeof(worker));
    if (session == 0) {
        return worker;
    }
    fl_product_login::ScopedTicket scoped = session->begin_scoped_operation("playback", now_ms);
    worker.operation_id = scoped.operation_id;
    worker.generation = scoped.generation;
    strncpy(worker.user_id, scoped.user_id, sizeof(worker.user_id) - 1);
    strncpy(worker.kind, scoped.kind, sizeof(worker.kind) - 1);
    return worker;
}

int worker_generation_matches(const fl_product_login::WorkerTicket &ticket, uint32_t generation)
{
    if (ticket.operation_id == 0) {
        return 0;
    }
    return ticket.generation == generation;
}

} /* namespace fl_product_inbox */
