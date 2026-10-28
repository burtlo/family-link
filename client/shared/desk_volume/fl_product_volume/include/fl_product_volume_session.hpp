#ifndef FL_PRODUCT_VOLUME_SESSION_HPP
#define FL_PRODUCT_VOLUME_SESSION_HPP

#include "fl_product_volume_storage.hpp"

namespace fl_product_volume
{

enum VolumeScreen { kVolumeScreenInbox = 0,
                    kVolumeScreenVolume = 1 };

typedef void (*SpeakerCodecCallback)(int codec);

class VolumeSession
{
  public:
    VolumeSession();

    void bind_storage(VolumeStorage *storage);
    void on_power_on();

    VolumeScreen screen() const;
    int stop_index() const;
    int speaker_codec() const;
    int shows_numeric_readout() const;

    void open_volume();
    int leave_volume();
    void set_stop_index(int index);
    void set_speaker_codec_callback(SpeakerCodecCallback callback);

    int playback_pause_armed() const;

  private:
    void notify_speaker_codec();

    VolumeStorage *storage_;
    VolumeScreen screen_;
    int stop_index_;
    int playback_pause_armed_;
    SpeakerCodecCallback speaker_codec_callback_;
};

} /* namespace fl_product_volume */

#endif
