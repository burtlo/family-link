#include "fl_product_record_storage.hpp"

#include <string.h>

namespace fl_product_record
{
namespace
{

const uint8_t kMagic[8] = {'F', 'L', 'R', 'E', 'C', '0', '1', 0};

void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xffu);
    p[1] = (uint8_t)((value >> 8) & 0xffu);
    p[2] = (uint8_t)((value >> 16) & 0xffu);
    p[3] = (uint8_t)((value >> 24) & 0xffu);
}

uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void put_u64(uint8_t *p, uint64_t value)
{
    put_u32(p, (uint32_t)value);
    put_u32(p + 4, (uint32_t)(value >> 32));
}

uint64_t read_u64(const uint8_t *p)
{
    return (uint64_t)read_u32(p) | ((uint64_t)read_u32(p + 4) << 32);
}

void copy_id(char *dst, const char *src)
{
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < kMaxClientMessageId) {
        dst[i] = src[i];
        i += 1;
    }
    dst[i] = '\0';
}

static_assert(sizeof(StoredObject) < 256, "stored object is metadata only");
static_assert(sizeof(AttachedStorage) < 16384, "attached storage must not hold a PCM or sketch image");

} /* namespace */

AttachedStorage::AttachedStorage()
    : media_(0), next_offset_((uint64_t)kMediaDataOrigin), in_progress_budget_(0), sketch_open_(0)
{
    memset(&in_progress_, 0, sizeof(in_progress_));
    memset(finished_, 0, sizeof(finished_));
    memset(window_, 0, sizeof(window_));
    memset(&pending_identity_, 0, sizeof(pending_identity_));
    memset(identities_, 0, sizeof(identities_));
}

void AttachedStorage::attach_media(MediaBackend *backend)
{
    media_ = backend;
    memset(&in_progress_, 0, sizeof(in_progress_));
    memset(finished_, 0, sizeof(finished_));
    memset(&pending_identity_, 0, sizeof(pending_identity_));
    memset(identities_, 0, sizeof(identities_));
    in_progress_budget_ = 0;
    sketch_open_ = 0;
    next_offset_ = (uint64_t)kMediaDataOrigin;
    if (media_ == 0) {
        return;
    }
    if (!load_directory()) {
        format_empty_directory();
    }
}

bool AttachedStorage::has_in_progress() const
{
    return in_progress_.valid != 0;
}

bool AttachedStorage::id_ok(const char *client_message_id) const
{
    size_t n = 0;
    if (client_message_id == 0) {
        return false;
    }
    while (client_message_id[n] != '\0') {
        n += 1;
        if (n >= kMaxClientMessageId) {
            return false;
        }
    }
    return n > 0;
}

int AttachedStorage::find_finished(const char *client_message_id) const
{
    size_t i;
    if (client_message_id == 0 || client_message_id[0] == '\0') {
        return -1;
    }
    for (i = 0; i < kAttachedFinishedSlots; ++i) {
        if (finished_[i].valid != 0 && strcmp(finished_[i].client_message_id, client_message_id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

uint64_t AttachedStorage::committed_end() const
{
    uint64_t end = (uint64_t)kMediaDataOrigin;
    size_t i;
    for (i = 0; i < kAttachedFinishedSlots; ++i) {
        if (finished_[i].valid == 0) {
            continue;
        }
        if (finished_[i].audio_bytes > 0) {
            const uint64_t audio_end = finished_[i].audio_offset + (uint64_t)finished_[i].audio_bytes;
            if (audio_end > end) {
                end = audio_end;
            }
        }
        if (finished_[i].sketch_bytes > 0) {
            const uint64_t sketch_end = finished_[i].sketch_offset + (uint64_t)finished_[i].sketch_bytes;
            if (sketch_end > end) {
                end = sketch_end;
            }
        }
    }
    return end;
}

bool AttachedStorage::write_span(uint64_t offset, const uint8_t *data, size_t len)
{
    if (media_ == 0) {
        return false;
    }
    while (len > 0) {
        size_t n = len;
        if (n > (size_t)kWriteWindowBytes) {
            n = (size_t)kWriteWindowBytes;
        }
        memcpy(window_, data, n);
        if (!media_->write_at(offset, window_, n)) {
            return false;
        }
        offset += (uint64_t)n;
        data += n;
        len -= n;
    }
    return true;
}

bool AttachedStorage::load_directory()
{
    uint8_t raw[kDirectoryBytes];
    StoredObject slots[kAttachedFinishedSlots];
    uint64_t loaded_next;
    size_t i;
    if (media_ == 0) {
        return false;
    }
    memset(raw, 0, sizeof(raw));
    if (!media_->read_at(0, raw, sizeof(raw))) {
        return false;
    }
    if (memcmp(raw, kMagic, sizeof(kMagic)) != 0) {
        return false;
    }
    if (read_u32(raw + 8) != 1u || read_u32(raw + 12) != (uint32_t)kAttachedFinishedSlots) {
        return false;
    }
    if (read_u64(raw + 16) != (uint64_t)kMediaDataOrigin) {
        return false;
    }
    loaded_next = read_u64(raw + 24);
    memset(slots, 0, sizeof(slots));
    for (i = 0; i < kAttachedFinishedSlots; ++i) {
        const uint8_t *slot = raw + kDirectoryHeaderBytes + (i * (size_t)kSlotBytes);
        size_t n;
        int saw_nul = 0;
        if (slot[72] > 1 || slot[73] > 1) {
            return false;
        }
        for (n = 0; n < kMaxClientMessageId; ++n) {
            slots[i].client_message_id[n] = (char)slot[n];
            if (slot[n] == 0) {
                saw_nul = 1;
                break;
            }
        }
        if (saw_nul == 0) {
            return false;
        }
        slots[i].audio_bytes = read_u32(slot + 48);
        slots[i].sketch_bytes = read_u32(slot + 52);
        slots[i].audio_offset = read_u64(slot + 56);
        slots[i].sketch_offset = read_u64(slot + 64);
        slots[i].uploading = slot[72] ? 1 : 0;
        slots[i].valid = slot[73] ? 1 : 0;
        if (slots[i].valid == 0) {
            memset(&slots[i], 0, sizeof(slots[i]));
            continue;
        }
        if (slots[i].client_message_id[0] == '\0' || slots[i].sketch_bytes > (uint32_t)kMaxFlsk2Bytes) {
            return false;
        }
        if (slots[i].audio_bytes > 0 && slots[i].audio_offset < (uint64_t)kMediaDataOrigin) {
            return false;
        }
        if (slots[i].sketch_bytes > 0 && slots[i].sketch_offset < (uint64_t)kMediaDataOrigin) {
            return false;
        }
    }
    memcpy(finished_, slots, sizeof(finished_));
    memset(identities_, 0, sizeof(identities_));
    (void)read_identity_block();
    next_offset_ = loaded_next;
    if (next_offset_ < committed_end()) {
        next_offset_ = committed_end();
    }
    if (next_offset_ < (uint64_t)kMediaDataOrigin) {
        next_offset_ = (uint64_t)kMediaDataOrigin;
    }
    if (next_offset_ != loaded_next) {
        return store_directory();
    }
    return true;
}

bool AttachedStorage::store_directory()
{
    uint8_t raw[kDirectoryBytes];
    size_t i;
    if (media_ == 0) {
        return false;
    }
    memset(raw, 0, sizeof(raw));
    memcpy(raw, kMagic, sizeof(kMagic));
    put_u32(raw + 8, 1u);
    put_u32(raw + 12, (uint32_t)kAttachedFinishedSlots);
    put_u64(raw + 16, (uint64_t)kMediaDataOrigin);
    put_u64(raw + 24, next_offset_);
    for (i = 0; i < kAttachedFinishedSlots; ++i) {
        uint8_t *slot = raw + kDirectoryHeaderBytes + (i * (size_t)kSlotBytes);
        size_t n;
        const StoredObject *obj = &finished_[i];
        for (n = 0; n < kMaxClientMessageId && obj->client_message_id[n] != '\0'; ++n) {
            slot[n] = (uint8_t)obj->client_message_id[n];
        }
        put_u32(slot + 48, obj->audio_bytes);
        put_u32(slot + 52, obj->sketch_bytes);
        put_u64(slot + 56, obj->audio_offset);
        put_u64(slot + 64, obj->sketch_offset);
        slot[72] = (uint8_t)(obj->uploading ? 1 : 0);
        slot[73] = (uint8_t)(obj->valid ? 1 : 0);
    }
    if (!media_->write_at(0, raw, sizeof(raw))) {
        return false;
    }
    if (!write_identity_block()) {
        return false;
    }
    return media_->flush();
}

void AttachedStorage::format_empty_directory()
{
    memset(finished_, 0, sizeof(finished_));
    memset(&in_progress_, 0, sizeof(in_progress_));
    memset(&pending_identity_, 0, sizeof(pending_identity_));
    memset(identities_, 0, sizeof(identities_));
    in_progress_budget_ = 0;
    sketch_open_ = 0;
    next_offset_ = (uint64_t)kMediaDataOrigin;
    (void)store_directory();
}

bool AttachedStorage::begin_in_progress(const char *client_message_id, uint32_t audio_budget_bytes)
{
    if (!id_ok(client_message_id) || has_in_progress() || media_ == 0) {
        return false;
    }
    if (!can_start_second_recording()) {
        return false;
    }
    if (find_finished(client_message_id) >= 0) {
        return false;
    }
    memset(&in_progress_, 0, sizeof(in_progress_));
    copy_id(in_progress_.client_message_id, client_message_id);
    in_progress_.audio_offset = next_offset_;
    in_progress_.valid = 1;
    in_progress_budget_ = audio_budget_bytes;
    sketch_open_ = 0;
    return true;
}

bool AttachedStorage::append_in_progress_audio(const uint8_t *data, size_t len)
{
    uint64_t at;
    if (!has_in_progress() || media_ == 0) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    if (data == 0 || sketch_open_ != 0) {
        return false;
    }
    if (in_progress_.audio_bytes > in_progress_budget_ ||
        len > (size_t)(in_progress_budget_ - in_progress_.audio_bytes)) {
        return false;
    }
    at = in_progress_.audio_offset + (uint64_t)in_progress_.audio_bytes;
    if (!write_span(at, data, len)) {
        return false;
    }
    in_progress_.audio_bytes += (uint32_t)len;
    return true;
}

bool AttachedStorage::write_in_progress_sketch(const uint8_t *sketch, size_t sketch_len)
{
    uint64_t base;
    uint64_t at;
    if (!has_in_progress() || media_ == 0) {
        return false;
    }
    if (sketch_len == 0) {
        return true;
    }
    if (sketch == 0) {
        return false;
    }
    if (in_progress_.sketch_bytes > (uint32_t)kMaxFlsk2Bytes ||
        sketch_len > (size_t)((uint32_t)kMaxFlsk2Bytes - in_progress_.sketch_bytes)) {
        return false;
    }
    if (sketch_open_ == 0) {
        base = in_progress_.audio_offset + (uint64_t)in_progress_.audio_bytes;
    } else {
        base = in_progress_.sketch_offset;
    }
    at = base + (uint64_t)in_progress_.sketch_bytes;
    if (!write_span(at, sketch, sketch_len)) {
        return false;
    }
    if (sketch_open_ == 0) {
        in_progress_.sketch_offset = base;
        sketch_open_ = 1;
    }
    in_progress_.sketch_bytes += (uint32_t)sketch_len;
    return true;
}

bool AttachedStorage::commit_in_progress_to_finished()
{
    size_t i;
    int slot = -1;
    StoredObject saved_in_progress;
    SendIdentity saved_identity;
    uint32_t saved_budget;
    int saved_sketch;
    uint64_t saved_next;
    if (!has_in_progress() || media_ == 0) {
        return false;
    }
    for (i = 0; i < kAttachedFinishedSlots; ++i) {
        if (finished_[i].valid == 0) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        return false;
    }
    /* Payload bytes are unsynced until the directory flush below makes them durable. */
    saved_in_progress = in_progress_;
    saved_identity = pending_identity_;
    saved_budget = in_progress_budget_;
    saved_sketch = sketch_open_;
    saved_next = next_offset_;
    finished_[slot] = in_progress_;
    finished_[slot].uploading = 0;
    finished_[slot].valid = 1;
    if (pending_identity_.valid != 0 &&
        strcmp(pending_identity_.client_message_id, finished_[slot].client_message_id) == 0) {
        identities_[slot] = pending_identity_;
    } else {
        memset(&identities_[slot], 0, sizeof(identities_[slot]));
    }
    memset(&pending_identity_, 0, sizeof(pending_identity_));
    memset(&in_progress_, 0, sizeof(in_progress_));
    in_progress_budget_ = 0;
    sketch_open_ = 0;
    next_offset_ = committed_end();
    if (!store_directory()) {
        memset(&finished_[slot], 0, sizeof(finished_[slot]));
        memset(&identities_[slot], 0, sizeof(identities_[slot]));
        pending_identity_ = saved_identity;
        in_progress_ = saved_in_progress;
        in_progress_budget_ = saved_budget;
        sketch_open_ = saved_sketch;
        next_offset_ = saved_next;
        return false;
    }
    return true;
}

void AttachedStorage::discard_in_progress()
{
    memset(&in_progress_, 0, sizeof(in_progress_));
    memset(&pending_identity_, 0, sizeof(pending_identity_));
    in_progress_budget_ = 0;
    sketch_open_ = 0;
    if (media_ != 0) {
        next_offset_ = committed_end();
    }
}

bool AttachedStorage::read_finished(const char *client_message_id, MediaKind kind, uint32_t offset, uint8_t *dest,
                                    size_t len, size_t *out_read)
{
    int index;
    const StoredObject *slot;
    uint64_t media_off;
    uint32_t total;
    uint32_t available;
    size_t n;
    if (out_read == 0 || media_ == 0) {
        return false;
    }
    *out_read = 0;
    index = find_finished(client_message_id);
    if (index < 0) {
        return false;
    }
    slot = &finished_[index];
    if (kind == kMediaAudio) {
        media_off = slot->audio_offset;
        total = slot->audio_bytes;
    } else if (kind == kMediaSketch) {
        media_off = slot->sketch_offset;
        total = slot->sketch_bytes;
    } else {
        return false;
    }
    if (offset > total) {
        return false;
    }
    if (len == 0 || offset == total) {
        return true;
    }
    if (dest == 0) {
        return false;
    }
    available = total - offset;
    n = len;
    if (n > (size_t)available) {
        n = (size_t)available;
    }
    if (!media_->read_at(media_off + (uint64_t)offset, dest, n)) {
        return false;
    }
    *out_read = n;
    return true;
}

bool AttachedStorage::can_start_second_recording() const
{
    if (has_in_progress()) {
        return false;
    }
    /* Room means a free finished slot. One uploading object plus that free
     * slot is the overlap case. Both slots occupied refuses another start.
     */
    return finished_count() < kAttachedFinishedSlots;
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

const StoredObject *AttachedStorage::finished_slot(int index) const
{
    if (index < 0 || index >= kAttachedFinishedSlots) {
        return 0;
    }
    return finished_[index].valid != 0 ? &finished_[index] : 0;
}

bool AttachedStorage::mark_finished_uploading(const char *client_message_id)
{
    int index;
    int previous;
    index = find_finished(client_message_id);
    if (index < 0) {
        return false;
    }
    previous = finished_[index].uploading;
    finished_[index].uploading = 1;
    if (!store_directory()) {
        finished_[index].uploading = previous;
        return false;
    }
    return true;
}

void AttachedStorage::clear_finished_upload(const char *client_message_id)
{
    int index;
    StoredObject previous;
    SendIdentity previous_identity;
    uint64_t previous_next;
    index = find_finished(client_message_id);
    if (index < 0) {
        return;
    }
    previous = finished_[index];
    previous_identity = identities_[index];
    previous_next = next_offset_;
    memset(&finished_[index], 0, sizeof(finished_[index]));
    memset(&identities_[index], 0, sizeof(identities_[index]));
    if (!has_in_progress()) {
        next_offset_ = committed_end();
    }
    if (!store_directory()) {
        finished_[index] = previous;
        identities_[index] = previous_identity;
        next_offset_ = previous_next;
    }
}

void AttachedStorage::set_in_progress_identity(const SendIdentity &identity)
{
    if (!has_in_progress() || identity.valid == 0 ||
        strcmp(identity.client_message_id, in_progress_.client_message_id) != 0) {
        return;
    }
    pending_identity_ = identity;
    if (pending_identity_.recipient_count < 0) {
        pending_identity_.recipient_count = 0;
    }
    if (pending_identity_.recipient_count > kIdentityRecipients) {
        pending_identity_.recipient_count = kIdentityRecipients;
    }
}

bool AttachedStorage::identity_for(const char *client_message_id, SendIdentity *out) const
{
    int index;
    if (out == 0) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    index = find_finished(client_message_id);
    if (index < 0 || identities_[index].valid == 0) {
        return false;
    }
    *out = identities_[index];
    return true;
}

bool AttachedStorage::write_identity_block()
{
    uint8_t raw[16 + kAttachedFinishedSlots * kIdentitySlotBytes];
    size_t i;
    static const uint8_t kIdentityMagic[8] = {'F', 'L', 'R', 'I', 'D', '0', '1', 0};
    if (media_ == 0) {
        return false;
    }
    static_assert(kIdentityOrigin + sizeof(raw) <= (size_t)kMediaDataOrigin, "identity overlaps media");
    memset(raw, 0, sizeof(raw));
    memcpy(raw, kIdentityMagic, sizeof(kIdentityMagic));
    put_u32(raw + 8, 1u);
    put_u32(raw + 12, (uint32_t)kAttachedFinishedSlots);
    for (i = 0; i < kAttachedFinishedSlots; ++i) {
        uint8_t *slot = raw + 16 + (i * (size_t)kIdentitySlotBytes);
        const SendIdentity *id = &identities_[i];
        int r;
        size_t n;
        for (n = 0; n < kMaxClientMessageId && id->client_message_id[n] != '\0'; ++n) {
            slot[n] = (uint8_t)id->client_message_id[n];
        }
        for (n = 0; n < kIdentityUser && id->sender_user_id[n] != '\0'; ++n) {
            slot[48 + n] = (uint8_t)id->sender_user_id[n];
        }
        slot[72] = (uint8_t)(id->recipient_count < 0 ? 0 : id->recipient_count);
        slot[73] = (uint8_t)(id->valid ? 1 : 0);
        for (r = 0; r < kIdentityRecipients; ++r) {
            for (n = 0; n < kIdentityUser && id->recipient_ids[r][n] != '\0'; ++n) {
                slot[80 + (r * kIdentityUser) + n] = (uint8_t)id->recipient_ids[r][n];
            }
            for (n = 0; n < kIdentityName && id->recipient_names[r][n] != '\0'; ++n) {
                slot[80 + (kIdentityRecipients * kIdentityUser) + (r * kIdentityName) + n] =
                    (uint8_t)id->recipient_names[r][n];
            }
        }
    }
    return media_->write_at((uint64_t)kIdentityOrigin, raw, sizeof(raw));
}

bool AttachedStorage::read_identity_block()
{
    uint8_t raw[16 + kAttachedFinishedSlots * kIdentitySlotBytes];
    size_t i;
    static const uint8_t kIdentityMagic[8] = {'F', 'L', 'R', 'I', 'D', '0', '1', 0};
    if (media_ == 0) {
        return false;
    }
    memset(raw, 0, sizeof(raw));
    if (!media_->read_at((uint64_t)kIdentityOrigin, raw, sizeof(raw)) ||
        memcmp(raw, kIdentityMagic, sizeof(kIdentityMagic)) != 0 || read_u32(raw + 8) != 1u) {
        memset(identities_, 0, sizeof(identities_));
        return false;
    }
    for (i = 0; i < kAttachedFinishedSlots; ++i) {
        const uint8_t *slot = raw + 16 + (i * (size_t)kIdentitySlotBytes);
        SendIdentity parsed;
        int r;
        size_t n;
        memset(&parsed, 0, sizeof(parsed));
        for (n = 0; n < kMaxClientMessageId && slot[n] != 0; ++n) {
            parsed.client_message_id[n] = (char)slot[n];
        }
        for (n = 0; n < kIdentityUser && slot[48 + n] != 0; ++n) {
            parsed.sender_user_id[n] = (char)slot[48 + n];
        }
        parsed.recipient_count = slot[72];
        parsed.valid = slot[73] ? 1 : 0;
        if (parsed.recipient_count > kIdentityRecipients) {
            parsed.recipient_count = 0;
            parsed.valid = 0;
        }
        for (r = 0; r < parsed.recipient_count; ++r) {
            for (n = 0; n < kIdentityUser && slot[80 + (r * kIdentityUser) + n] != 0; ++n) {
                parsed.recipient_ids[r][n] = (char)slot[80 + (r * kIdentityUser) + n];
            }
            for (n = 0; n < kIdentityName &&
                        slot[80 + (kIdentityRecipients * kIdentityUser) + (r * kIdentityName) + n] != 0;
                 ++n) {
                parsed.recipient_names[r][n] =
                    (char)slot[80 + (kIdentityRecipients * kIdentityUser) + (r * kIdentityName) + n];
            }
        }
        if (parsed.valid == 0 || parsed.client_message_id[0] == '\0' ||
            (finished_[i].valid != 0 && strcmp(parsed.client_message_id, finished_[i].client_message_id) != 0)) {
            memset(&identities_[i], 0, sizeof(identities_[i]));
            continue;
        }
        identities_[i] = parsed;
    }
    return true;
}

uint32_t AttachedStorage::in_progress_audio_budget() const
{
    return has_in_progress() ? in_progress_budget_ : 0;
}

uint32_t AttachedStorage::in_progress_audio_bytes() const
{
    return has_in_progress() ? in_progress_.audio_bytes : 0;
}

uint32_t AttachedStorage::in_progress_sketch_bytes() const
{
    return has_in_progress() ? in_progress_.sketch_bytes : 0;
}

} /* namespace fl_product_record */
