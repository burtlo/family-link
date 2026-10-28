#ifndef FL_PRODUCT_INBOX_WORKERS_HPP
#define FL_PRODUCT_INBOX_WORKERS_HPP

#include "fl_product_login_workers.hpp"

namespace fl_product_inbox
{

fl_product_login::WorkerTicket begin_inbox_worker(fl_product_login::SessionWriter *session,
                                                  int64_t now_ms);
fl_product_login::WorkerTicket begin_media_worker(fl_product_login::SessionWriter *session,
                                                  int64_t now_ms);
fl_product_login::WorkerTicket begin_playback_worker(fl_product_login::SessionWriter *session,
                                                     int64_t now_ms);
int worker_generation_matches(const fl_product_login::WorkerTicket &ticket, uint32_t generation);

} /* namespace fl_product_inbox */

#endif
