#ifndef FL_PRODUCT_LOGIN_CATALOG_HPP
#define FL_PRODUCT_LOGIN_CATALOG_HPP

#include "fl_product_login_session.hpp"

namespace fl_product_login
{

/* Virtual-clock catalog and on-screen lab controls.
   Not the session-owner interface. Fixture URLs and HTTP stay in the firmware harness. */

enum ManualAction {
    kManualWifiDown = 1,
    kManualWifiReturn,
    kManualEmpty,
    kManualOne,
    kManualEight,
    kManualAdd,
    kManualRemove,
    kManualReorder,
    kManualFailOnce,
    kManualFailTwice,
    kManualInvalid,
    kManualHold,
    kManualMute,
    kManualUnmute,
    kManualIdleClock,
    kManualStaleBeats
};

void run_scripted_catalog(SessionWriter *session);
/* Returns the hold deadline in model milliseconds, or -1 when the hold did not start.
   Other actions return 0. */
int64_t apply_manual_control(SessionWriter *session, int action);
void complete_manual_hold(SessionWriter *session, int64_t deadline_ms);

} /* namespace fl_product_login */

#endif
