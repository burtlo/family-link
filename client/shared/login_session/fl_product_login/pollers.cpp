#include "fl_product_login_pollers.hpp"

namespace fl_product_login {

RosterPoller::RosterPoller(SessionWriter *session) : session_(session) {}

bool RosterPoller::submit(const RosterResult &result)
{
    return session_->enqueue_roster(result);
}

HealthPoller::HealthPoller(SessionWriter *session) : session_(session) {}

void HealthPoller::submit_ack()
{
    session_->ack_health();
}

} /* namespace fl_product_login */
