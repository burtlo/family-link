#include "fl_product_record_storage.hpp"

#include <string.h>

namespace fl_product_record {

AttachedStorage::AttachedStorage()
{
    memset(&in_progress_, 0, sizeof(in_progress_));
    memset(finished_, 0, sizeof(finished_));
}

bool AttachedStorage::has_in_progress() const { return in_progress_.valid != 0; }

bool AttachedStorage::begin_in_progress(const char *client_message_id, uint32_t audio_budget_bytes)
{
    if (client_message_id == 0 || client_message_id[0] == '\0' || has_in_progress()) {
        return false;
    }
    if (!can_start_second_recording()) {
        return false;
    }
    memset(&in_progress_, 0, sizeof(in_progress_));
    strncpy(in_progress_.client_message_id, client_message_id, sizeof(in_progress_.client_message_id) - 1);
    in_progress_.audio_bytes_reserved = audio_budget_bytes;
    in_progress_.valid = 1;
    return true;
}

bool AttachedStorage::write_in_progress_sketch(const uint8_t *sketch, size_t sketch_len)
{
    if (!has_in_progress() || sketch == 0 || sketch_len > kMaxFlsk2Bytes) {
        return false;
    }
    memcpy(in_progress_.sketch, sketch, sketch_len);
    in_progress_.sketch_bytes = (uint32_t)sketch_len;
    return true;
}

bool AttachedStorage::commit_in_progress_to_finished()
{
    size_t i;
    if (!has_in_progress()) {
        return false;
    }
    for (i = 0; i < kAttachedFinishedSlots; ++i) {
        if (finished_[i].valid == 0) {
            finished_[i] = in_progress_;
            memset(&in_progress_, 0, sizeof(in_progress_));
            return true;
        }
    }
    return false;
}

void AttachedStorage::discard_in_progress() { memset(&in_progress_, 0, sizeof(in_progress_)); }

bool AttachedStorage::can_start_second_recording() const
{
    size_t i;
    if (has_in_progress()) {
        return false;
    }
    if (finished_count() < kAttachedFinishedSlots) {
        return true;
    }
    for (i = 0; i < kAttachedFinishedSlots; ++i) {
        if (finished_[i].valid != 0 && finished_[i].uploading != 0) {
            return true;
        }
    }
    return false;
}

int AttachedStorage::finished_count() const
{
    int count = 0;
    size_t i;
    for (i = 0; i < kAttachedFinishedSlots; ++i) {
        if (finished_[i].valid != 0) {
            count += 1;
        }
    }
    return count;
}

const StoredPayload *AttachedStorage::finished_slot(int index) const
{
    if (index < 0 || index >= kAttachedFinishedSlots) {
        return 0;
    }
    return finished_[index].valid != 0 ? &finished_[index] : 0;
}

bool AttachedStorage::mark_finished_uploading(const char *client_message_id)
{
    size_t i;
    if (client_message_id == 0) {
        return false;
    }
    for (i = 0; i < kAttachedFinishedSlots; ++i) {
        if (finished_[i].valid != 0 && strcmp(finished_[i].client_message_id, client_message_id) == 0) {
            finished_[i].uploading = 1;
            return true;
        }
    }
    return false;
}

void AttachedStorage::clear_finished_upload(const char *client_message_id)
{
    size_t i;
    if (client_message_id == 0) {
        return;
    }
    for (i = 0; i < kAttachedFinishedSlots; ++i) {
        if (finished_[i].valid != 0 && strcmp(finished_[i].client_message_id, client_message_id) == 0) {
            memset(&finished_[i], 0, sizeof(finished_[i]));
            return;
        }
    }
}

uint32_t AttachedStorage::in_progress_audio_budget() const
{
    return has_in_progress() ? in_progress_.audio_bytes_reserved : 0;
}

} /* namespace fl_product_record */
