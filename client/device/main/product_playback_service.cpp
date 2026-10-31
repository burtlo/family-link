#include "product_playback_service.hpp"

#include "fl_product_inbox_flsk2.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace product_ui
{
namespace
{
constexpr int kCacheCap = 65536;
constexpr int kPushBudget = 2048;
constexpr int kFramePixels = 120 * 120;
constexpr uint32_t kBytesPerMs = 32;

void copyText(char *destination, int cap, const char *source)
{
    if (!destination || cap <= 0)
        return;
    destination[0] = 0;
    if (!source)
        return;
    std::snprintf(destination, (size_t)cap, "%s", source);
}

uint32_t readU32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}
} // namespace

PlaybackService::PlaybackService(MediaStore &media, DisplaySurfaceBroker &surfaces, PlaybackTransport transport,
                                 AudioEndpoint audio)
    : media_(media), surfaces_(surfaces), transport_(transport), audio_(audio)
{
}

PlaybackService::~PlaybackService()
{
    clearCaches();
}

void PlaybackService::setUnready(void (*notify)(void *), void *context)
{
    unready_ = notify;
    unready_context_ = context;
}

void PlaybackService::clearCaches()
{
    for (auto &cache : caches_) {
        std::free(cache.bytes);
        cache = {};
    }
}

PlaybackService::Progress *PlaybackService::find(const char *message_id)
{
    if (!message_id || !message_id[0])
        return nullptr;
    for (auto &progress : progress_)
        if (progress.used && std::strcmp(progress.id, message_id) == 0)
            return &progress;
    return nullptr;
}

const PlaybackService::Progress *PlaybackService::find(const char *message_id) const
{
    if (!message_id || !message_id[0])
        return nullptr;
    for (const auto &progress : progress_)
        if (progress.used && std::strcmp(progress.id, message_id) == 0)
            return &progress;
    return nullptr;
}

PlaybackService::Progress *PlaybackService::ensure(const char *message_id)
{
    if (Progress *found = find(message_id))
        return found;
    for (auto &progress : progress_) {
        if (!progress.used) {
            progress = {};
            copyText(progress.id, (int)sizeof(progress.id), message_id);
            progress.used = true;
            progress.held_ms = -1;
            return &progress;
        }
    }
    return nullptr;
}

uint32_t PlaybackService::nextOperation()
{
    if (++next_operation_ == 0)
        ++next_operation_;
    return next_operation_;
}

int PlaybackService::clockMs() const
{
    if (playing_ && audio_.played_ms)
        return audio_.played_ms(audio_.context);
    const Progress *progress = find(selected_);
    return progress ? progress->position_ms : 0;
}

void PlaybackService::rememberClock()
{
    if (Progress *progress = find(selected_)) {
        const int ms = clockMs();
        if (ms > progress->position_ms)
            progress->position_ms = ms;
    }
}

void PlaybackService::stopOutput()
{
    if (output_open_ && audio_.stop)
        audio_.stop(audio_.context);
    output_open_ = false;
    playing_ = false;
    buffering_ = false;
}

void PlaybackService::failAudio()
{
    rememberClock();
    stopOutput();
    cant_play_until_ = now_ + kCantPlayMs;
}

void PlaybackService::pausePreserving()
{
    if (!playing_ && !buffering_)
        return;
    rememberClock();
    if (output_open_ && audio_.pause)
        audio_.pause(audio_.context);
    output_open_ = false;
    playing_ = false;
    buffering_ = false;
}

void PlaybackService::signOut()
{
    stopOutput();
    if (transport_.request_quiesce)
        transport_.request_quiesce();
    audio_job_ = {};
    sketch_job_ = {};
    for (auto &progress : progress_)
        progress = {};
    clearCaches();
    media_.discardSession();
    selected_[0] = 0;
    framed_[0] = 0;
    current_ = {};
    user_[0] = 0;
    bearer_[0] = 0;
    cant_play_until_ = 0;
    stopping_ = false;
    media_allowed_ = false;
    carousel_ = false;
    ++epoch_;
    if (epoch_ == 0)
        epoch_ = 1;
    generation_ = 0;
    media_.setPlaybackAllowed(false);
}

void PlaybackService::setSession(const char *user, uint32_t generation, const char *bearer, bool media_allowed,
                                 int codec_volume, int64_t now)
{
    now_ = now;
    const bool have_user = user && user[0];
    if (user_[0] && (!have_user || std::strcmp(user_, user) != 0))
        signOut();
    const bool starting = have_user && !user_[0];
    if (have_user)
        copyText(user_, (int)sizeof(user_), user);
    // generation_ tags submitted jobs. A mismatched result is dropped in apply().
    generation_ = generation;
    copyText(bearer_, (int)sizeof(bearer_), bearer);
    const bool lost = media_allowed_ && !media_allowed;
    media_allowed_ = media_allowed;
    media_.setPlaybackAllowed(media_allowed);
    if (lost)
        pausePreserving();
    if (starting && transport_.begin_session)
        transport_.begin_session();
    codec_volume_ = codec_volume;
    if (codec_volume != applied_volume_) {
        applied_volume_ = codec_volume;
        if (audio_.set_volume)
            audio_.set_volume(audio_.context, codec_volume_, codec_volume_ <= 0);
    }
}

void PlaybackService::bind(const PlaybackMedia *media, bool carousel_visible)
{
    carousel_ = carousel_visible;
    if (!carousel_visible)
        pausePreserving();
    if (!media || !media->message_id[0]) {
        if (playing_ || buffering_)
            pausePreserving();
        selected_[0] = 0;
        current_ = {};
        return;
    }
    if (std::strcmp(selected_, media->message_id) != 0) {
        if (playing_ || buffering_)
            pausePreserving();
        copyText(selected_, (int)sizeof(selected_), media->message_id);
        playing_ = false;
        buffering_ = false;
    }
    current_ = *media;
}

void PlaybackService::loadCache(const char *message_id)
{
    const uint32_t stored = media_.stored(message_id, MediaKind::Sketch);
    InkCache *slot = nullptr;
    for (auto &cache : caches_) {
        if (cache.used && std::strcmp(cache.id, message_id) == 0) {
            slot = &cache;
            break;
        }
    }
    if (stored == 0 || stored > kCacheCap) {
        if (slot) {
            std::free(slot->bytes);
            *slot = {};
        }
        return;
    }
    if (slot && slot->length == (int)stored)
        return;
    if (!slot) {
        for (auto &cache : caches_) {
            if (!cache.used) {
                slot = &cache;
                break;
            }
        }
    }
    if (!slot)
        return;
    auto *bytes = static_cast<uint8_t *>(std::malloc(stored));
    if (!bytes)
        return;
    uint32_t filled = 0;
    while (filled < stored) {
        const int got = media_.readSketch(message_id, filled, bytes + filled,
                                          (int)(stored - filled) > MediaStore::kReadCap ? MediaStore::kReadCap
                                                                                        : (int)(stored - filled));
        if (got <= 0)
            break;
        filled += (uint32_t)got;
    }
    if (filled != stored) {
        std::free(bytes);
        return;
    }
    std::free(slot->bytes);
    *slot = {};
    copyText(slot->id, (int)sizeof(slot->id), message_id);
    slot->bytes = bytes;
    slot->length = (int)stored;
    slot->used = true;
}

void PlaybackService::noteVisible(const char *const *message_ids, int count)
{
    bool keep[3] = {};
    for (int i = 0; i < 3; ++i) {
        if (!caches_[i].used)
            continue;
        for (int n = 0; n < count; ++n) {
            if (message_ids && message_ids[n] && std::strcmp(caches_[i].id, message_ids[n]) == 0)
                keep[i] = true;
        }
        if (!keep[i]) {
            std::free(caches_[i].bytes);
            caches_[i] = {};
        }
    }
    for (int n = 0; n < count; ++n) {
        const char *id = message_ids ? message_ids[n] : nullptr;
        if (!id || !inkFor(id))
            continue;
        loadCache(id);
    }
}

void PlaybackService::drainMaintenance()
{
    if (maintenance_open_ || audio_job_.open || sketch_job_.open || !media_submit_)
        return;
    char path[96];
    if (!media_.popDiscard(path))
        return;
    product_inbox_http::Job job{};
    job.kind = product_inbox_http::kPlaybackMediaMaintenanceJob;
    std::snprintf(job.file_path, sizeof(job.file_path), "%s", path);
    if (!media_submit_(&job, media_submit_context_)) {
        media_.restoreDiscard(path);
        return;
    }
    maintenance_open_ = true;
}

void PlaybackService::onMediaResult(const product_inbox_http::Result &result)
{
    if (result.kind == product_inbox_http::kPlaybackMediaMaintenanceJob) {
        maintenance_open_ = false;
        return;
    }
    if (result.kind != product_inbox_http::kPlaybackMediaJob)
        return;
    Inflight *job = nullptr;
    if (audio_job_.open && result.operation_id == audio_job_.operation)
        job = &audio_job_;
    else if (sketch_job_.open && result.operation_id == sketch_job_.operation)
        job = &sketch_job_;
    if (!job)
        return;
    const bool audio = job->kind == kAudioKind;
    const auto kind = audio ? MediaKind::Audio : MediaKind::Sketch;
    const bool current = result.generation == generation_ && job->epoch == epoch_ &&
                         std::strcmp(result.message_id, job->id) == 0;
    char message[48];
    copyText(message, (int)sizeof(message), job->id);
    *job = {};
    if (!current || stopping_ || !media_allowed_) {
        media_.abandon(message, kind);
        return;
    }
    if (result.http_status == 401 || result.http_status == 403) {
        media_.abandon(message, kind);
        pausePreserving();
        if (unready_)
            unready_(unready_context_);
        return;
    }
    const bool http_ok = result.perform_ok && (result.http_status == 200 || result.http_status == 206) &&
                         result.bytes_written > 0;
    if (!http_ok || !result.hash_matches) {
        if (result.hash_matches)
            media_.abandon(message, kind);
        else
            (void)media_.noteWritten(message, kind, result.bytes_written > 0 ? result.bytes_written : 1, true, false);
        if (audio)
            failAudio();
        else if (Progress *progress = find(message))
            progress->sketch_fault = true;
        return;
    }
    const auto noted = media_.noteWritten(message, kind, result.bytes_written, result.full_asset != 0, true);
    if (noted == MediaStore::Note::Rejected || noted == MediaStore::Note::Failed) {
        if (audio)
            failAudio();
        else if (Progress *progress = find(message))
            progress->sketch_fault = true;
    }
}

bool PlaybackService::submit(int kind)
{
    Inflight *job = kind == kAudioKind ? &audio_job_ : &sketch_job_;
    if (audio_job_.open || sketch_job_.open || stopping_ || !media_allowed_ || !selected_[0])
        return job->open;
    const bool audio = kind == kAudioKind;
    const uint32_t total = audio ? current_.audio_bytes : current_.sketch_bytes;
    if (total == 0)
        return false;
    if (!audio && !current_.sketch)
        return false;
    if (audio && current_.audio_bytes == 0)
        return false;
    const auto media_kind = audio ? MediaKind::Audio : MediaKind::Sketch;
    if (media_.complete(selected_, media_kind) || media_.failed(selected_, media_kind))
        return false;
    const uint32_t stored = media_.stored(selected_, media_kind);
    if (total > 0 && stored >= total)
        return false;
    if (!media_submit_ || !media_.prepare(selected_, media_kind, total, audio ? current_.audio_sha : current_.sketch_sha))
        return false;
    uint32_t operation = media_.operation(selected_, media_kind);
    if (operation == 0) {
        operation = nextOperation();
        media_.setOperation(selected_, media_kind, operation);
    }
    product_inbox_http::Job request{};
    request.kind = product_inbox_http::kPlaybackMediaJob;
    request.operation_id = operation;
    request.generation = generation_;
    request.asset = audio ? 0 : 1;
    request.range_start = media_.stored(selected_, media_kind);
    request.asset_bytes = total;
    request.timeout_ms = 6000;
    copyText(request.method, (int)sizeof(request.method), "GET");
    copyText(request.message_id, (int)sizeof(request.message_id), selected_);
    copyText(request.bearer, (int)sizeof(request.bearer), bearer_);
    copyText(request.sha256, (int)sizeof(request.sha256), audio ? current_.audio_sha : current_.sketch_sha);
    std::snprintf(request.path, sizeof(request.path), "/v2/messages/%s/media/%s", selected_, audio ? "audio" : "sketch");
    if (!media_.filePath(selected_, media_kind, request.file_path, (int)sizeof(request.file_path)))
        return false;
    const uint32_t slice_end = request.range_start + 16383u < total ? request.range_start + 16383u : total - 1u;
    std::snprintf(request.range_header, sizeof(request.range_header), "bytes=%" PRIu32 "-%" PRIu32, request.range_start,
                  slice_end);
    if (!media_submit_(&request, media_submit_context_))
        return false;
    job->open = true;
    job->operation = request.operation_id;
    job->epoch = epoch_;
    copyText(job->id, (int)sizeof(job->id), selected_);
    job->kind = kind;
    return true;
}

bool PlaybackService::ensureWorker()
{
    if (!started_worker_ && transport_.start)
        started_worker_ = transport_.start();
    if (media_submit_)
        return true;
    return started_worker_ || !transport_.start;
}

void PlaybackService::pumpAudio()
{
    if (!playing_ || !output_open_ || !media_allowed_ || !audio_.accept)
        return;
    uint8_t buffer[512];
    int budget = kPushBudget;
    while (budget > 0) {
        const uint32_t offset = (uint32_t)origin_ms_ * kBytesPerMs + pushed_;
        const uint32_t have = media_.stored(selected_, MediaKind::Audio);
        if (offset >= have)
            break;
        int ask = budget < (int)sizeof(buffer) ? budget : (int)sizeof(buffer);
        if (have - offset < (uint32_t)ask)
            ask = (int)(have - offset);
        const int got = media_.readAudio(selected_, offset, buffer, ask);
        if (got <= 0)
            break;
        const int accepted = audio_.accept(audio_.context, buffer, got);
        if (accepted < 0) {
            failAudio();
            return;
        }
        if (accepted == 0)
            break;
        pushed_ += (uint32_t)accepted;
        budget -= accepted;
    }
    if (Progress *progress = find(selected_)) {
        const int ms = clockMs();
        if (ms > progress->position_ms)
            progress->position_ms = ms;
        if (current_.duration_ms > 0 && progress->position_ms >= (int)current_.duration_ms) {
            progress->position_ms = (int)current_.duration_ms;
            progress->completed = true;
            progress->caught_up = false;
            stopOutput();
        }
    }
}

void PlaybackService::toggle()
{
    if (stopping_ || !media_allowed_ || !carousel_ || !selected_[0])
        return;
    if (playing_ || buffering_) {
        pausePreserving();
        return;
    }
    if (current_.audio_bytes == 0 || current_.duration_ms == 0) {
        failAudio();
        return;
    }
    if (current_.audio_format[0] && std::strcmp(current_.audio_format, "pcm_s16le") != 0) {
        failAudio();
        return;
    }
    if (!ensureWorker()) {
        failAudio();
        return;
    }
    Progress *progress = ensure(selected_);
    if (!progress)
        return;
    if (progress->completed) {
        progress->completed = false;
        progress->position_ms = 0;
        progress->painted = 0;
        progress->ink = false;
        progress->reset_frame = true;
        progress->caught_up = false;
        progress->sketch_fault = false;
        progress->last_t = 0;
        progress->held_ms = -1;
    }
    progress->engaged = true;
    buffering_ = true;
    const uint32_t have = media_.stored(selected_, MediaKind::Audio);
    const bool prefix = have >= current_.audio_bytes || have >= (uint32_t)kPrefixBytes;
    if (!prefix) {
        if (!media_submit_ && have == 0) {
            failAudio();
            return;
        }
        (void)submit(kAudioKind);
        if (current_.sketch)
            (void)submit(kSketchKind);
        return;
    }
    if (!audio_.start || !audio_.start(audio_.context, progress->position_ms, codec_volume_, codec_volume_ <= 0)) {
        failAudio();
        return;
    }
    origin_ms_ = progress->position_ms;
    pushed_ = 0;
    output_open_ = true;
    playing_ = true;
    buffering_ = false;
    if (current_.sketch && !media_.complete(selected_, MediaKind::Sketch))
        (void)submit(kSketchKind);
    if (!media_.complete(selected_, MediaKind::Audio))
        (void)submit(kAudioKind);
}

void PlaybackService::advance(int64_t now)
{
    now_ = now;
    drainMaintenance();
    if (stopping_) {
        const bool idle = !transport_.quiescent || transport_.quiescent();
        if (idle) {
            audio_job_ = {};
            sketch_job_ = {};
        }
        return;
    }
    if (!media_allowed_)
        pausePreserving();
    if (cant_play_until_ > 0 && now_ >= cant_play_until_)
        cant_play_until_ = 0;
    if (selected_[0] && media_allowed_ && (playing_ || buffering_)) {
        const uint32_t have = media_.stored(selected_, MediaKind::Audio);
        const bool prefix = have >= current_.audio_bytes || have >= (uint32_t)kPrefixBytes;
        if (buffering_ && prefix && !media_.failed(selected_, MediaKind::Audio)) {
            Progress *progress = ensure(selected_);
            if (progress && audio_.start &&
                audio_.start(audio_.context, progress->position_ms, codec_volume_, codec_volume_ <= 0)) {
                origin_ms_ = progress->position_ms;
                pushed_ = 0;
                output_open_ = true;
                playing_ = true;
                buffering_ = false;
            } else if (progress) {
                failAudio();
            }
        }
        if (!media_.complete(selected_, MediaKind::Audio) && !media_.failed(selected_, MediaKind::Audio))
            (void)submit(kAudioKind);
        if (current_.sketch && !media_.complete(selected_, MediaKind::Sketch) &&
            !media_.failed(selected_, MediaKind::Sketch))
            (void)submit(kSketchKind);
    }
    pumpAudio();
}

void PlaybackService::paint(uint16_t *pixels, int pixel_count)
{
    Progress *progress = find(selected_);
    if (!pixels || pixel_count < kFramePixels || !progress || !progress->engaged ||
        progress->sketch_fault || !current_.sketch)
        return;
    const bool switched = std::strcmp(framed_, selected_) != 0;
    if (progress->reset_frame || switched) {
        std::memset(pixels, 0, (size_t)kFramePixels * sizeof(uint16_t));
        progress->painted = 0;
        progress->ink = false;
        progress->caught_up = false;
        progress->last_t = 0;
        progress->held_ms = -1;
        progress->reset_frame = false;
        copyText(framed_, (int)sizeof(framed_), selected_);
    }
    const int position = progress->completed ? (int)current_.duration_ms : clockMs();
    if (progress->caught_up && progress->held_ms >= position)
        return;
    progress->caught_up = false;
    uint8_t chunk[MediaStore::kReadCap];
    while (!progress->sketch_fault) {
        const int got = media_.readSketch(selected_, (uint32_t)progress->painted * 8, chunk, (int)sizeof(chunk));
        if (got < 8) {
            if (media_.complete(selected_, MediaKind::Sketch) || media_.failed(selected_, MediaKind::Sketch)) {
                progress->caught_up = true;
                progress->held_ms = position;
            }
            return;
        }
        const int available = got - (got % 8);
        int usable = 0;
        uint32_t previous = progress->last_t;
        bool timed = progress->painted > 0;
        for (int index = 0; index < available / 8; ++index) {
            const uint8_t *point = chunk + index * 8;
            const uint32_t time = readU32(point + 4);
            if (point[0] >= 120 || point[1] >= 120 || (timed && time < previous)) {
                progress->sketch_fault = true;
                break;
            }
            if (!progress->completed && (int)time > position)
                break;
            previous = time;
            timed = true;
            usable = (index + 1) * 8;
        }
        if (usable > 0) {
            uint16_t rendered = 0;
            fl_product_inbox::flsk2_paint_prefix(pixels, (size_t)pixel_count, chunk, (size_t)usable,
                                                 progress->completed ? 0xffffffffu : (uint32_t)position, &rendered);
            if (rendered <= 0) {
                progress->sketch_fault = true;
                progress->caught_up = true;
                progress->held_ms = position;
                return;
            }
            progress->painted += rendered;
            progress->last_t = previous;
            progress->ink = true;
        }
        if (progress->sketch_fault || usable < available || got < (int)sizeof(chunk)) {
            progress->caught_up = true;
            progress->held_ms = position;
            return;
        }
    }
}

void PlaybackService::visitInk(const char *message_id, void (*visit)(void *, int, int, uint16_t), void *context) const
{
    const Progress *progress = find(message_id);
    if (!progress || !progress->ink || !visit)
        return;
    const InkCache *cache = nullptr;
    for (const auto &slot : caches_) {
        if (slot.used && std::strcmp(slot.id, message_id) == 0)
            cache = &slot;
    }
    if (!cache)
        return;
    const int limit = progress->completed ? 0x7fffffff : progress->position_ms;
    uint32_t previous = 0;
    bool timed = false;
    for (int offset = 0; offset + 8 <= cache->length; offset += 8) {
        const uint8_t *point = cache->bytes + offset;
        const uint32_t time = readU32(point + 4);
        if (point[0] >= 120 || point[1] >= 120 || (timed && time < previous))
            return;
        if ((int)time > limit)
            return;
        previous = time;
        timed = true;
        const uint16_t color = (uint16_t)point[2] | ((uint16_t)point[3] << 8);
        visit(context, point[0], point[1], color);
    }
}

bool PlaybackService::inkFor(const char *message_id) const
{
    const Progress *progress = find(message_id);
    return progress && progress->ink;
}

bool PlaybackService::needsFrame() const
{
    const Progress *progress = find(selected_);
    return progress && progress->engaged && current_.sketch && (progress->ink || !progress->sketch_fault);
}

PlaybackSnapshot PlaybackService::snapshot() const
{
    PlaybackSnapshot snap;
    const bool selected = selected_[0] != 0;
    snap.playing = playing_;
    snap.positionMs = clockMs();
    snap.cantPlay = media_allowed_ && cant_play_until_ > now_;
    snap.enabled = selected && media_allowed_ && !stopping_;
    if (!selected)
        snap.transport = PlaybackSnapshot::Transport::Hidden;
    else if (playing_ || buffering_)
        snap.transport = PlaybackSnapshot::Transport::Pause;
    else
        snap.transport = PlaybackSnapshot::Transport::Play;
    if (!carousel_)
        snap.enabled = false;
    return snap;
}

void PlaybackService::requestStop()
{
    stopping_ = true;
    rememberClock();
    stopOutput();
    if (transport_.request_quiesce)
        transport_.request_quiesce();
}

bool PlaybackService::quiesced() const
{
    if (!stopping_ || audio_job_.open || sketch_job_.open)
        return false;
    return !transport_.quiescent || transport_.quiescent();
}
} // namespace product_ui
