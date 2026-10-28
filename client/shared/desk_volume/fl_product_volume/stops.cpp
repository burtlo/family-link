#include "fl_product_volume_stops.hpp"

namespace fl_product_volume
{
namespace
{

constexpr int kStops[kVolumeStopCount] = {
    0, 78, 80, 82, 84, 86, 88, 90, 92, 94, 96, 98, 100};

} /* namespace */

int volume_stop_index_default()
{
    return kVolumeStopCount - 1;
}

int volume_codec_for_stop_index(int index)
{
    if (index < 0) {
        return kStops[0];
    }
    if (index >= kVolumeStopCount) {
        return kStops[kVolumeStopCount - 1];
    }
    return kStops[index];
}

int volume_stop_index_from_codec(int codec)
{
    int i;
    if (codec <= 0) {
        return 0;
    }
    for (i = 1; i < kVolumeStopCount; ++i) {
        if (codec == kStops[i]) {
            return i;
        }
        if (codec < kStops[i]) {
            return i - 1;
        }
    }
    return kVolumeStopCount - 1;
}

int volume_stop_index_clamp(int index)
{
    if (index < 0) {
        return 0;
    }
    if (index >= kVolumeStopCount) {
        return kVolumeStopCount - 1;
    }
    return index;
}

} /* namespace fl_product_volume */
