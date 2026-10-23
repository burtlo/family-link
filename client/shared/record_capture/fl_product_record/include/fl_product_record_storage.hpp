#ifndef FL_PRODUCT_RECORD_STORAGE_HPP
#define FL_PRODUCT_RECORD_STORAGE_HPP

#include "fl_product_record_types.hpp"

#include <stddef.h>
#include <stdint.h>

namespace fl_product_record {

struct StoredPayload {
    char client_message_id[kMaxClientMessageId];
    uint32_t audio_bytes_reserved;
    uint32_t sketch_bytes;
    uint8_t sketch[kMaxFlsk2Bytes];
    int uploading;
    int valid;
};

class AttachedStorage {
public:
    AttachedStorage();

    bool has_in_progress() const;
    bool begin_in_progress(const char *client_message_id, uint32_t audio_budget_bytes);
    bool write_in_progress_sketch(const uint8_t *sketch, size_t sketch_len);
    bool commit_in_progress_to_finished();
    void discard_in_progress();

    bool can_start_second_recording() const;
    int finished_count() const;
    const StoredPayload *finished_slot(int index) const;
    bool mark_finished_uploading(const char *client_message_id);
    void clear_finished_upload(const char *client_message_id);

    uint32_t in_progress_audio_budget() const;

private:
    StoredPayload in_progress_;
    StoredPayload finished_[kAttachedFinishedSlots];
};

} /* namespace fl_product_record */

#endif
