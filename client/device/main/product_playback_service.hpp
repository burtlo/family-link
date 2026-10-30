#pragma once

#include "product_display_surface.hpp"
#include "product_media_store.hpp"

#include <cstdint>

namespace product_ui
{

// Immutable description of the selected message. Copied at bind time.
struct PlaybackMedia {
    char message_id[48]{};
    char audio_format[16]{};
    char audio_sha[65]{};
    char sketch_sha[65]{};
    uint32_t duration_ms = 0;
    uint32_t audio_bytes = 0;
    uint32_t sketch_bytes = 0;
    bool sketch = false;
};

struct PlaybackSnapshot {
    enum class Transport { Hidden,
                           Play,
                           Pause };
    Transport transport = Transport::Hidden;
    bool enabled = false;
    bool cantPlay = false;
    bool playing = false;
    int positionMs = 0;
};

// Non-blocking PCM sink. played_ms is the sample clock, not the UI tick.
// accept returns bytes taken, 0 when the queue refuses, or <0 on a fault.
struct AudioEndpoint {
    void *context = nullptr;
    bool (*start)(void *context, int position_ms, int codec_volume, bool silent) = nullptr;
    void (*pause)(void *context) = nullptr;
    void (*stop)(void *context) = nullptr;
    int (*played_ms)(void *context) = nullptr;
    int (*accept)(void *context, const uint8_t *pcm, int bytes) = nullptr;
    void (*set_volume)(void *context, int codec_volume, bool silent) = nullptr;
};

struct PlaybackFetchJob {
    uint32_t operation = 0;
    uint32_t generation = 0;
    uint32_t epoch = 0;
    char message_id[48]{};
    char path[96]{};
    char bearer[48]{};
    char sha256[65]{};
    char range_header[40]{};
    uint32_t range_start = 0;
    uint32_t asset_bytes = 0;
    int kind = 0;
    int timeout_ms = 6000;
};

struct PlaybackFetchResult {
    uint32_t operation = 0;
    uint32_t generation = 0;
    uint32_t epoch = 0;
    char message_id[48]{};
    int kind = 0;
    int perform_ok = 0;
    int http_status = 0;
    int terminal = 1;
    int data_len = 0;
    uint8_t *data = nullptr;
};

struct PlaybackTransport {
    bool (*start)() = nullptr;
    bool (*submit)(const PlaybackFetchJob *job) = nullptr;
    const PlaybackFetchResult *(*poll)() = nullptr;
    void (*release)(const PlaybackFetchResult *result) = nullptr;
    void (*request_quiesce)() = nullptr;
    bool (*quiescent)() = nullptr;
};

// Session playback for one signed-in generation. Sketch pixels are not kept
// per message: a paused card is rebuilt by replaying stored flsk2 points into
// the single leased surface. That replay is the checkpoint.
class PlaybackService
{
  public:
    static constexpr int kAudioKind = 1;
    static constexpr int kSketchKind = 2;
    static constexpr int kPrefixBytes = 32000;
    static constexpr int kCantPlayMs = 2000;

    PlaybackService(MediaStore &media, DisplaySurfaceBroker &surfaces, PlaybackTransport transport = {},
                    AudioEndpoint audio = {});
    ~PlaybackService();
    PlaybackService(const PlaybackService &) = delete;
    PlaybackService &operator=(const PlaybackService &) = delete;

    void setTransport(PlaybackTransport transport)
    {
        transport_ = transport;
    }
    void setAudio(AudioEndpoint audio)
    {
        audio_ = audio;
    }
    void setUnready(void (*notify)(void *), void *context);
    void setSession(uint32_t generation, const char *bearer, bool media_allowed, int codec_volume, int64_t now);
    void signOut();
    void bind(const PlaybackMedia *media, bool carousel_visible);
    void noteVisible(const char *const *message_ids, int count);
    void pausePreserving();
    void toggle();
    void advance(int64_t now);
    // Replay flsk2 into a caller buffer. Safe for the leased surface; does not allocate one per message.
    void paint(uint16_t *pixels, int pixel_count);
    void visitInk(const char *message_id, void (*visit)(void *, int x, int y, uint16_t color), void *context) const;
    bool inkFor(const char *message_id) const;
    bool needsFrame() const;
    PlaybackSnapshot snapshot() const;
    // Returns immediately. Queues and tasks drop only after quiescence is acknowledged.
    void requestStop();
    bool quiesced() const;
    DisplaySurfaceBroker &surfaces()
    {
        return surfaces_;
    }
    uint32_t generation() const
    {
        return generation_;
    }

  private:
    struct Progress {
        char id[48]{};
        int position_ms = 0;
        int painted = 0;
        bool used = false;
        bool engaged = false;
        bool ink = false;
        bool completed = false;
        bool sketch_fault = false;
        bool reset_frame = false;
        bool caught_up = false;
        uint32_t last_t = 0;
        int held_ms = -1;
    };
    struct Inflight {
        uint32_t operation = 0;
        uint32_t epoch = 0;
        char id[48]{};
        int kind = 0;
        bool open = false;
    };
    struct InkCache {
        char id[48]{};
        uint8_t *bytes = nullptr;
        int length = 0;
        bool used = false;
    };

    Progress *find(const char *message_id);
    const Progress *find(const char *message_id) const;
    Progress *ensure(const char *message_id);
    int clockMs() const;
    void rememberClock();
    void failAudio();
    void stopOutput();
    void releaseResult(const PlaybackFetchResult *result);
    void apply(const PlaybackFetchResult &result);
    bool submit(int kind);
    bool ensureWorker();
    void pumpAudio();
    void loadCache(const char *message_id);
    void clearCaches();
    uint32_t nextOperation();

    MediaStore &media_;
    DisplaySurfaceBroker &surfaces_;
    PlaybackTransport transport_;
    AudioEndpoint audio_;
    void (*unready_)(void *) = nullptr;
    void *unready_context_ = nullptr;
    Progress progress_[64]{};
    Inflight audio_job_{};
    Inflight sketch_job_{};
    InkCache caches_[3]{};
    PlaybackMedia current_{};
    char bearer_[48]{};
    char selected_[48]{};
    char framed_[48]{};
    uint32_t generation_ = 0;
    uint32_t epoch_ = 1;
    uint32_t next_operation_ = 0;
    int codec_volume_ = 0;
    int origin_ms_ = 0;
    uint32_t pushed_ = 0;
    int64_t now_ = 0;
    int64_t cant_play_until_ = 0;
    bool media_allowed_ = false;
    bool carousel_ = false;
    bool playing_ = false;
    bool buffering_ = false;
    bool stopping_ = false;
    bool output_open_ = false;
    bool started_worker_ = false;
};

} // namespace product_ui
