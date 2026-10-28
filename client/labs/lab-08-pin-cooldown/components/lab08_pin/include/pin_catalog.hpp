#ifndef LAB08_PIN_CATALOG_HPP
#define LAB08_PIN_CATALOG_HPP

#include "session.hpp"

#include <cstdint>

namespace lab08
{

enum ManualAction {
    kManualSelectUser = 1,
    kManualDigit,
    kManualBoot,
    kManualWrongPin,
    kManualMute,
    kManualUnmute,
    kManualSwitchUser,
    kManualAdvanceCopy,
    kManualAdvanceCooldown,
    kManualAdvanceIdle
};

void prime_roster(lab07::SessionWriter *session);
void run_clock_catalog(lab07::SessionWriter *session);
void run_model_catalog(lab07::SessionWriter *session);
int64_t apply_manual_pin(lab07::SessionWriter *session, int action);
struct PinTraceAccount {
    uint32_t dropped;
    uint32_t oversized;
    uint32_t malformed;
};

void emit_pin_traces(const lab07::SessionWriter &session, PinTraceAccount *account = nullptr);

} /* namespace lab08 */

#endif
