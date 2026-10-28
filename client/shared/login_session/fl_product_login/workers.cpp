#include "fl_product_login_workers.hpp"

#include <string.h>

namespace fl_product_login
{

WorkerTicket begin_inbox_worker(SessionWriter *session, int64_t now_ms)
{
    WorkerTicket worker;
    memset(&worker, 0, sizeof(worker));
    if (session == 0) {
        return worker;
    }
    ScopedTicket scoped = session->begin_scoped_operation("inbox", now_ms);
    worker.operation_id = scoped.operation_id;
    worker.generation = scoped.generation;
    strncpy(worker.user_id, scoped.user_id, sizeof(worker.user_id) - 1);
    strncpy(worker.kind, scoped.kind, sizeof(worker.kind) - 1);
    return worker;
}

WorkerTicket begin_send_worker(SessionWriter *session, int64_t now_ms)
{
    WorkerTicket worker;
    memset(&worker, 0, sizeof(worker));
    if (session == 0) {
        return worker;
    }
    ScopedTicket scoped = session->begin_scoped_operation("profile", now_ms);
    worker.operation_id = scoped.operation_id;
    worker.generation = scoped.generation;
    strncpy(worker.user_id, scoped.user_id, sizeof(worker.user_id) - 1);
    strncpy(worker.kind, scoped.kind, sizeof(worker.kind) - 1);
    return worker;
}

int deliver_worker_completion(SessionWriter *session, const WorkerTicket &ticket, uint32_t generation,
                              int http_status, int64_t now_ms)
{
    ScopedResult result;
    if (session == 0 || ticket.operation_id == 0) {
        return 0;
    }
    memset(&result, 0, sizeof(result));
    result.operation_id = ticket.operation_id;
    result.generation = generation;
    strncpy(result.user_id, ticket.user_id, sizeof(result.user_id) - 1);
    strncpy(result.kind, ticket.kind, sizeof(result.kind) - 1);
    strncpy(result.session_id, session->session_id(), sizeof(result.session_id) - 1);
    result.http_status = http_status;
    result.parse_ok = 1;
    result.body_ok = 1;
    return session->deliver_scoped_completion(result, now_ms);
}

} /* namespace fl_product_login */
