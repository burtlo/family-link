#ifndef FL_PRODUCT_JOURNEY_HPP
#define FL_PRODUCT_JOURNEY_HPP

#include "fl_product_inbox_carousel.hpp"
#include "fl_product_login_session.hpp"
#include "fl_product_send_journey.hpp"
#include "fl_product_volume_journey.hpp"
#include "fl_product_volume_storage.hpp"
#include "fl_shell_model.h"

#include <stdint.h>

namespace fl_product_journey
{

/* Host-side composition of promoted product modules (P3–P9) for scripted runs. */

class HostJourney
{
  public:
    HostJourney();

    void reset(int boot_muted);
    void advance(int64_t now_ms);

    fl_product_login::SessionWriter &login();
    fl_product_inbox::CarouselSession &inbox();
    fl_product_volume::DeskVolumeJourney &volume();
    fl_product_volume::VolumeStorage &volume_storage();
    fl_product_send::SendJourney &send();
    fl_shell_session_t &shell();

    void shell_goto(fl_shell_stage_t stage);
    fl_shell_stage_t shell_stage() const;

    int signed_in() const;
    int volume_screen_active() const;

  private:
    fl_product_login::SessionWriter login_;
    fl_product_inbox::CarouselSession inbox_;
    fl_product_volume::VolumeStorage volume_storage_;
    fl_product_volume::DeskVolumeJourney volume_;
    fl_product_send::SendJourney send_;
    fl_shell_session_t shell_;
    int64_t now_ms_;
};

int run_scripted_scenario(const char *scenario_id);

} /* namespace fl_product_journey */

#endif
