#pragma once

#include <cstdint>

namespace product_ui
{

enum class MediaKind : uint8_t { Audio = 0,
                                 Sketch = 1 };

// File-backed session cache. Audio bytes stay on storage; callers read a
// bounded chunk. Playback progress is not stored here. Audio reads return
// nothing while playback is disallowed, including when a prefix is already
// on disk. Sketch reads stay available so a paused card can be reconstructed.
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
    void discardSession();

    bool begin(const char *messageId, MediaKind kind, uint32_t total, const char *sha256);
    // Contiguous append only. The completing write checks SHA-256.
    enum class Append { Ok,
                        Complete,
                        HashMismatch,
                        Rejected };
    Append append(const char *messageId, MediaKind kind, uint32_t offset, const uint8_t *data, int length);
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
        char sha[65]{};
        bool used = false;
        bool complete = false;
        bool failed = false;
        bool interrupted = false;
    };
    Asset *find(const char *messageId, MediaKind kind);
    const Asset *find(const char *messageId, MediaKind kind) const;
    bool path(const Asset &asset, char *out, int cap) const;
    int read(const Asset &asset, uint32_t offset, uint8_t *destination, int capacity) const;
    bool hashFile(const Asset &asset, char out[65]) const;

    char root_[192]{};
    Asset assets_[kMaxAssets]{};
    bool playback_allowed_ = false;
};

} // namespace product_ui
