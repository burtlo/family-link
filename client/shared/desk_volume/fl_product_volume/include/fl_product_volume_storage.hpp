#ifndef FL_PRODUCT_VOLUME_STORAGE_HPP
#define FL_PRODUCT_VOLUME_STORAGE_HPP

namespace fl_product_volume
{

class VolumeStorage
{
  public:
    VolumeStorage();

    void reset_for_tests();
    void set_fail_next_persist(int fail);

    int has_saved_stop() const;
    int saved_stop_index() const;

    int load_stop_index(int *index_out);
    int persist_stop_index(int index);

  private:
    int has_saved_;
    int saved_index_;
    int fail_next_;
};

} /* namespace fl_product_volume */

#endif
