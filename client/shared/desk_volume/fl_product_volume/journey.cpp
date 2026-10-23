#include "fl_product_volume_journey.hpp"

#include "fl_product_inbox_carousel.hpp"
#include "fl_product_login_session.hpp"

namespace fl_product_volume {

DeskVolumeJourney::DeskVolumeJourney()
{
    connectivity_lost_while_volume_ = 0;
}

void DeskVolumeJourney::bind_storage(VolumeStorage *storage) { session_.bind_storage(storage); }

void DeskVolumeJourney::on_power_on() { session_.on_power_on(); }

VolumeScreen DeskVolumeJourney::screen() const { return session_.screen(); }

int DeskVolumeJourney::stop_index() const { return session_.stop_index(); }

int DeskVolumeJourney::speaker_codec() const { return session_.speaker_codec(); }

int DeskVolumeJourney::playback_pause_armed() const { return session_.playback_pause_armed(); }

int DeskVolumeJourney::shows_numeric_readout() const { return session_.shows_numeric_readout(); }

int DeskVolumeJourney::enter_from_inbox(fl_product_inbox::CarouselSession &inbox, int64_t now_ms)
{
    if (session_.screen() == kVolumeScreenVolume) {
        return 0;
    }
    inbox.pause_for_settings(now_ms);
    session_.open_volume();
    return 1;
}

int DeskVolumeJourney::leave_to_inbox(fl_product_inbox::CarouselSession &inbox, int64_t now_ms)
{
    (void)inbox;
    (void)now_ms;
    return session_.leave_volume();
}

int DeskVolumeJourney::mute_from_volume(fl_product_inbox::CarouselSession &inbox,
                                          fl_product_login::SessionWriter &login, int64_t now_ms)
{
    if (session_.screen() != kVolumeScreenVolume) {
        return 0;
    }
    (void)session_.leave_volume();
    inbox.on_mute(now_ms);
    login.mute(now_ms);
    connectivity_lost_while_volume_ = 0;
    return 1;
}

void DeskVolumeJourney::set_stop_index(int index) { session_.set_stop_index(index); }

void DeskVolumeJourney::set_speaker_codec_callback(SpeakerCodecCallback callback)
{
    session_.set_speaker_codec_callback(callback);
}

int DeskVolumeJourney::on_red_record()
{
    if (session_.screen() == kVolumeScreenVolume) {
        return 0;
    }
    return 1;
}

void DeskVolumeJourney::note_connectivity_lost()
{
    if (session_.screen() == kVolumeScreenVolume) {
        connectivity_lost_while_volume_ = 1;
    }
}

int DeskVolumeJourney::volume_body_visible() const
{
    if (session_.screen() != kVolumeScreenVolume) {
        return 0;
    }
    return 1;
}

} /* namespace fl_product_volume */
