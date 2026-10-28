#include "fl_product_volume_storage.hpp"

#include "fl_product_volume_stops.hpp"

namespace fl_product_volume
{

VolumeStorage::VolumeStorage()
{
    reset_for_tests();
}

void VolumeStorage::reset_for_tests()
{
    has_saved_ = 0;
    saved_index_ = volume_stop_index_default();
    fail_next_ = 0;
}

void VolumeStorage::set_fail_next_persist(int fail)
{
    fail_next_ = fail ? 1 : 0;
}

int VolumeStorage::has_saved_stop() const
{
    return has_saved_;
}

int VolumeStorage::saved_stop_index() const
{
    return saved_index_;
}

int VolumeStorage::load_stop_index(int *index_out)
{
    if (index_out == 0) {
        return -1;
    }
    if (!has_saved_) {
        *index_out = volume_stop_index_default();
        return 0;
    }
    *index_out = saved_index_;
    return 0;
}

int VolumeStorage::persist_stop_index(int index)
{
    index = volume_stop_index_clamp(index);
    if (fail_next_) {
        fail_next_ = 0;
        return -1;
    }
    has_saved_ = 1;
    saved_index_ = index;
    return 0;
}

} /* namespace fl_product_volume */
