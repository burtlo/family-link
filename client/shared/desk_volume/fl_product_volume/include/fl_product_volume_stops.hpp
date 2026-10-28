#ifndef FL_PRODUCT_VOLUME_STOPS_HPP
#define FL_PRODUCT_VOLUME_STOPS_HPP

namespace fl_product_volume
{

enum { kVolumeStopCount = 13 };

int volume_stop_index_default();
int volume_stop_index_from_codec(int codec);
int volume_codec_for_stop_index(int index);
int volume_stop_index_clamp(int index);

} /* namespace fl_product_volume */

#endif
