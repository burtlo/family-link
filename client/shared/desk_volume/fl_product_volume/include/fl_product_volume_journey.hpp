#ifndef FL_PRODUCT_VOLUME_JOURNEY_HPP
#define FL_PRODUCT_VOLUME_JOURNEY_HPP

#include "fl_product_volume_session.hpp"

#include <stdint.h>

namespace fl_product_inbox
{
class CarouselSession;
}

namespace fl_product_login
{
class SessionWriter;
}

namespace fl_product_volume
{

class DeskVolumeJourney
{
  public:
    DeskVolumeJourney();

    void bind_storage(VolumeStorage *storage);
    void on_power_on();

    VolumeScreen screen() const;
    int stop_index() const;
    int speaker_codec() const;
    int playback_pause_armed() const;
    int shows_numeric_readout() const;

    int enter_from_inbox(fl_product_inbox::CarouselSession &inbox, int64_t now_ms);
    int leave_to_inbox(fl_product_inbox::CarouselSession &inbox, int64_t now_ms);
    int mute_from_volume(fl_product_inbox::CarouselSession &inbox,
                         fl_product_login::SessionWriter &login, int64_t now_ms);

    void set_stop_index(int index);
    void set_speaker_codec_callback(SpeakerCodecCallback callback);
    int on_red_record();
    void note_connectivity_lost();

    int volume_body_visible() const;

  private:
    VolumeSession session_;
    int connectivity_lost_while_volume_;
};

} /* namespace fl_product_volume */

#endif
