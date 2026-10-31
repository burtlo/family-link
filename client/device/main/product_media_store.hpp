#pragma once

#include <cstdint>

namespace product_ui
{

enum class MediaKind : uint8_t { Audio = 0,
                                 Sketch = 1 };

// In-memory record of files the inbox HTTP worker owns. The presentation tick
// updates these fields only. Directory creation, writes, reads, hashing, and
// deletion run on that worker. Audio reads stay available for the audio task
// until playback stops reading on the tick; sketch reads stay for paint until
// the selected-card buffer replaces them.
class MediaStore
{
  public:
    static constexpr int kReadCap = 4096;
    static constexpr int kMaxAssets = 128;

    explicit MediaStore(const char *root);
    void setPlaybackAllowed(bool allowed);
    bool playbackAllowed() const
    {
        return playback_allowed_;
    }
    void setIoProbe(void (*probe)(void *), void *context)
    {
        io_probe_ = probe;
        io_context_ = context;
    }
    // Clears the in-memory records and queues their paths for the worker.
    void discardSession();
    bool popDiscard(char out[96]);
    void restoreDiscard(const char path[96]);

    bool prepare(const char *messageId, MediaKind kind, uint32_t total, const char *sha256);
    bool filePath(const char *messageId, MediaKind kind, char *out, int cap) const;
    uint32_t operation(const char *messageId, MediaKind kind) const;
    void setOperation(const char *messageId, MediaKind kind, uint32_t operation);
    // Drops stored bytes and the operation id, and queues the file for deletion.
    void abandon(const char *messageId, MediaKind kind);
    enum class Note { Ok,
                      Complete,
                      Failed,
                      Rejected };
    Note noteWritten(const char *messageId, MediaKind kind, uint32_t added, bool full, bool hash_matches);
    void interrupt(const char *messageId, MediaKind kind);
    bool complete(const char *messageId, MediaKind kind) const;
    bool failed(const char *messageId, MediaKind kind) const;
    uint32_t stored(const char *messageId, MediaKind kind) const;
    int readAudio(const char *messageId, uint32_t offset, uint8_t *destination, int capacity) const;
    int readSketch(const char *messageId, uint32_t offset, uint8_t *destination, int capacity) const;
    int stagingBytes() const
    {
        return kReadCap;
    }

  private:
    struct Asset {
        char id[48]{};
        MediaKind kind = MediaKind::Audio;
        uint32_t total = 0;
        uint32_t stored = 0;
        uint32_t operation = 0;
        char sha[65]{};
        bool used = false;
        bool complete = false;
        bool failed = false;
        bool interrupted = false;
    };
    Asset *find(const char *messageId, MediaKind kind);
    const Asset *find(const char *messageId, MediaKind kind) const;
    bool path(const Asset &asset, char *out, int cap) const;
    void pushPending(const Asset &asset);
    int read(const Asset &asset, uint32_t offset, uint8_t *destination, int capacity) const;

    char root_[192]{};
    Asset assets_[kMaxAssets]{};
    char pending_[kMaxAssets][96]{};
    int pending_count_ = 0;
    bool playback_allowed_ = false;
    void (*io_probe_)(void *) = nullptr;
    void *io_context_ = nullptr;
};

} // namespace product_ui
