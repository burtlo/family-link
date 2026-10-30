#ifndef FL_PRODUCT_RECORD_STORAGE_HPP
#define FL_PRODUCT_RECORD_STORAGE_HPP

#include "fl_product_record_media.hpp"
#include "fl_product_record_types.hpp"

#include <stddef.h>
#include <stdint.h>

namespace fl_product_record
{

enum MediaKind {
    kMediaAudio = 1,
    kMediaSketch = 2
};

/* Metadata only. Audio and sketch bytes live in the attached MediaBackend. */
struct StoredObject {
    char client_message_id[kMaxClientMessageId];
    uint32_t audio_bytes;
    uint32_t sketch_bytes;
    uint64_t audio_offset;
    uint64_t sketch_offset;
    int uploading;
    int valid;
};

/* Sender and frozen recipients for a committed take. Written in the same
 * flush as the directory, in the reserved gap before media bytes. */
enum { kIdentityRecipients = 7,
       kIdentityUser = 24,
       kIdentityName = 48 };

struct SendIdentity {
    char client_message_id[kMaxClientMessageId];
    char sender_user_id[kIdentityUser];
    char recipient_ids[kIdentityRecipients][kIdentityUser];
    char recipient_names[kIdentityRecipients][kIdentityName];
    int recipient_count;
    int valid;
};

class AttachedStorage
{
  public:
    AttachedStorage();

    void attach_media(MediaBackend *backend);

    bool has_in_progress() const;
    bool begin_in_progress(const char *client_message_id, uint32_t audio_budget_bytes);
    bool append_in_progress_audio(const uint8_t *data, size_t len);
    bool write_in_progress_sketch(const uint8_t *sketch, size_t sketch_len);
    bool commit_in_progress_to_finished();
    void discard_in_progress();
    /* Copied onto the finished slot at commit. Ignored unless a take is open. */
    void set_in_progress_identity(const SendIdentity &identity);
    bool identity_for(const char *client_message_id, SendIdentity *out) const;

    /* Later send phase: stream one finished object in part-sized reads.
     * `offset` is within that object, not a media-file offset.
     * A short read sets *out_read below len at the end of the object.
     */
    bool read_finished(const char *client_message_id, MediaKind kind, uint32_t offset, uint8_t *dest,
                       size_t len, size_t *out_read);

    bool can_start_second_recording() const;
    int finished_count() const;
    const StoredObject *finished_slot(int index) const;
    bool mark_finished_uploading(const char *client_message_id);
    void clear_finished_upload(const char *client_message_id);

    uint32_t in_progress_audio_budget() const;
    uint32_t in_progress_audio_bytes() const;
    uint32_t in_progress_sketch_bytes() const;

  private:
    enum {
        kWriteWindowBytes = 4096,
        kMediaDataOrigin = 4096,
        kDirectoryBytes = 320,
        kDirectoryHeaderBytes = 64,
        kSlotBytes = 128,
        kIdentityOrigin = 512,
        kIdentitySlotBytes = 640
    };

    bool id_ok(const char *client_message_id) const;
    int find_finished(const char *client_message_id) const;
    uint64_t committed_end() const;
    bool write_span(uint64_t offset, const uint8_t *data, size_t len);
    bool load_directory();
    bool store_directory();
    void format_empty_directory();
    bool write_identity_block();
    bool read_identity_block();

    MediaBackend *media_;
    uint64_t next_offset_;
    uint32_t in_progress_budget_;
    int sketch_open_;
    uint8_t window_[kWriteWindowBytes];
    StoredObject in_progress_;
    StoredObject finished_[kAttachedFinishedSlots];
    SendIdentity pending_identity_;
    SendIdentity identities_[kAttachedFinishedSlots];
};

} /* namespace fl_product_record */

#endif
