#include "fl_product_volume_session.hpp"

#include "fl_product_volume_stops.hpp"

namespace fl_product_volume
{

VolumeSession::VolumeSession()
{
    storage_ = 0;
    screen_ = kVolumeScreenInbox;
    stop_index_ = volume_stop_index_default();
    playback_pause_armed_ = 0;
    speaker_codec_callback_ = 0;
}

void VolumeSession::notify_speaker_codec()
{
    if (speaker_codec_callback_ != 0) {
        speaker_codec_callback_(speaker_codec());
    }
}

void VolumeSession::set_speaker_codec_callback(SpeakerCodecCallback callback)
{
    speaker_codec_callback_ = callback;
    notify_speaker_codec();
}

void VolumeSession::bind_storage(VolumeStorage *storage)
{
    storage_ = storage;
}

void VolumeSession::on_power_on()
{
    int loaded = volume_stop_index_default();
    if (storage_ != 0) {
        (void)storage_->load_stop_index(&loaded);
    }
    stop_index_ = volume_stop_index_clamp(loaded);
    screen_ = kVolumeScreenInbox;
    playback_pause_armed_ = 0;
}

VolumeScreen VolumeSession::screen() const
{
    return screen_;
}

int VolumeSession::stop_index() const
{
    return stop_index_;
}

int VolumeSession::speaker_codec() const
{
    return volume_codec_for_stop_index(stop_index_);
}

int VolumeSession::shows_numeric_readout() const
{
    return 0;
}

void VolumeSession::open_volume()
{
    screen_ = kVolumeScreenVolume;
    playback_pause_armed_ = 1;
}

int VolumeSession::leave_volume()
{
    if (screen_ != kVolumeScreenVolume) {
        return 0;
    }
    if (storage_ != 0) {
        (void)storage_->persist_stop_index(stop_index_);
    }
    screen_ = kVolumeScreenInbox;
    playback_pause_armed_ = 0;
    return 1;
}

void VolumeSession::set_stop_index(int index)
{
    stop_index_ = volume_stop_index_clamp(index);
    notify_speaker_codec();
}

int VolumeSession::playback_pause_armed() const
{
    return playback_pause_armed_;
}

} /* namespace fl_product_volume */
