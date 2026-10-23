#ifndef FL_PRODUCT_LOGIN_POLLERS_HPP
#define FL_PRODUCT_LOGIN_POLLERS_HPP

#include "fl_product_login_session.hpp"

namespace fl_product_login {

/* Roster and health pollers only enqueue observations.
   They do not apply session stage transitions. */

class RosterPoller {
public:
    explicit RosterPoller(SessionWriter *session);
    bool submit(const RosterResult &result);

private:
    SessionWriter *session_;
};

class HealthPoller {
public:
    explicit HealthPoller(SessionWriter *session);
    void submit_ack();

private:
    SessionWriter *session_;
};

} /* namespace fl_product_login */

#endif
