#include "product_inbox_view_controller.hpp"
#include "product_playback_service.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <sys/stat.h>
#include <vector>

namespace
{
using namespace product_ui;

struct FailAlloc {
    bool fail = false;
    int calls = 0;
    size_t last = 0;
};
void *failingAllocate(size_t bytes, SurfacePlace, SurfacePlace *placed, void *context)
{
    auto *state = static_cast<FailAlloc *>(context);
    ++state->calls;
    state->last = bytes;
    if (state->fail)
        return nullptr;
    if (placed)
        *placed = SurfacePlace::Host;
    return std::malloc(bytes);
}
void failingRelease(void *block, void *)
{
    std::free(block);
}

void surfaceTests()
{
    DisplaySurfaceBroker broker;
    SurfaceLease first{};
    SurfaceLease second{};
    assert(!broker.acquire("inbox", SurfacePlace::Psram, &first));
    assert(broker.acquire("inbox", SurfacePlace::Host, &first));
    assert(first.bytes == DisplaySurfaceBroker::kBytes);
    assert(first.width == 120 && first.height == 120);
    assert(first.generation != 0);
    assert(!broker.acquire("other", SurfacePlace::Host, &second));
    assert(broker.acquireScratch("scratch", SurfacePlace::Host, &second));
    assert(broker.heldCount() == 2);
    assert(broker.addConsumer(&first));
    SurfaceLease stale = first;
    assert(!broker.release(&first));
    assert(!broker.transfer(&first, "next"));
    assert(broker.removeConsumer(&first));
    assert(broker.transfer(&first, "next"));
    assert(broker.pixels(stale) == nullptr);
    assert(!broker.release(&stale));
    assert(broker.release(&first));
    assert(!broker.release(&first));
    assert(broker.release(&second));
    assert(broker.heldCount() == 0);
    FailAlloc fail;
    fail.fail = true;
    DisplaySurfaceBroker empty;
    empty.setAllocator(failingAllocate, failingRelease, &fail);
    assert(!empty.acquire("inbox", SurfacePlace::Host, &first));
    assert(empty.heldCount() == 0);
    assert(fail.last == DisplaySurfaceBroker::kBytes);
}

void putPoint(uint8_t *point, uint8_t x, uint8_t y, uint16_t color, uint32_t time)
{
    point[0] = x;
    point[1] = y;
    point[2] = (uint8_t)color;
    point[3] = (uint8_t)(color >> 8);
    point[4] = (uint8_t)time;
    point[5] = (uint8_t)(time >> 8);
    point[6] = (uint8_t)(time >> 16);
    point[7] = (uint8_t)(time >> 24);
}

void makeParents(const char *file)
{
    char copy[256];
    std::snprintf(copy, sizeof(copy), "%s", file);
    for (char *slash = copy + 1; *slash; ++slash) {
        if (*slash == '/') {
            *slash = 0;
            mkdir(copy, 0755);
            *slash = '/';
        }
    }
}

void writeFile(const char *path, const uint8_t *data, int length, bool truncate)
{
    makeParents(path);
    FILE *handle = std::fopen(path, truncate ? "wb" : "ab");
    assert(handle);
    assert(std::fwrite(data, 1, (size_t)length, handle) == (size_t)length);
    std::fclose(handle);
}

void plant(MediaStore &store, const char *id, MediaKind kind, uint32_t total, const uint8_t *data, int length, bool full)
{
    assert(store.prepare(id, kind, total, ""));
    char path[256];
    assert(store.filePath(id, kind, path, (int)sizeof(path)));
    writeFile(path, data, length, store.stored(id, kind) == 0);
    assert(store.noteWritten(id, kind, (uint32_t)length, full, true) != MediaStore::Note::Rejected);
}

void mediaTests()
{
    MediaStore store("playback-host-media");
    store.discardSession();
    store.setPlaybackAllowed(true);
    const char *sha = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    assert(store.prepare("m1", MediaKind::Audio, 3, sha));
    char path[256];
    assert(store.filePath("m1", MediaKind::Audio, path, (int)sizeof(path)));
    writeFile(path, reinterpret_cast<const uint8_t *>("ab"), 2, true);
    assert(store.noteWritten("m1", MediaKind::Audio, 2, false, true) == MediaStore::Note::Ok);
    store.interrupt("m1", MediaKind::Audio);
    assert(!store.complete("m1", MediaKind::Audio));
    assert(store.stored("m1", MediaKind::Audio) == 2);
    writeFile(path, reinterpret_cast<const uint8_t *>("c"), 1, false);
    assert(store.noteWritten("m1", MediaKind::Audio, 1, true, true) == MediaStore::Note::Complete);
    uint8_t big[8000];
    assert(store.readAudio("m1", 0, big, 8000) == 3);
    assert(std::memcmp(big, "abc", 3) == 0);
    store.setPlaybackAllowed(false);
    assert(store.readAudio("m1", 0, big, 8) == 0);
    assert(store.stored("m1", MediaKind::Audio) == 3);
    store.setPlaybackAllowed(true);
    assert(store.prepare("bad", MediaKind::Sketch, 3, sha));
    assert(store.noteWritten("bad", MediaKind::Sketch, 3, true, false) == MediaStore::Note::Failed);
    assert(store.failed("bad", MediaKind::Sketch));
    assert(store.stored("bad", MediaKind::Sketch) == 0);
    assert(store.readSketch("bad", 0, big, 8) == 0);
    store.discardSession();
    assert(store.stored("m1", MediaKind::Audio) == 0);
}

struct FetchState {
    std::vector<product_inbox_http::Job> jobs;
    bool refuse = false;
    bool ack = false;
    int submits = 0;
};
bool fetchStart()
{
    return true;
}
FetchState *g_fetch;
bool submitMedia(const product_inbox_http::Job *job, void *)
{
    ++g_fetch->submits;
    if (g_fetch->refuse || !job)
        return false;
    g_fetch->jobs.push_back(*job);
    return true;
}
void requestQuiesce() {}
bool isQuiescent()
{
    return g_fetch->ack;
}

struct Clock {
    int origin = 0;
    int consumed = 0;
    bool running = false;
    bool fault = false;
    int volume = -1;
    bool silent = false;
    int cap = 4096;
    std::vector<uint8_t> queued;
    int starts = 0;
};
bool clockStart(void *context, int position, int volume, bool silent)
{
    auto *clock = static_cast<Clock *>(context);
    clock->origin = position;
    clock->consumed = 0;
    clock->running = true;
    clock->fault = false;
    clock->volume = volume;
    clock->silent = silent;
    clock->queued.clear();
    ++clock->starts;
    return true;
}
void clockPause(void *context)
{
    static_cast<Clock *>(context)->running = false;
}
void clockStop(void *context)
{
    auto *clock = static_cast<Clock *>(context);
    clock->running = false;
    clock->queued.clear();
}
int clockMs(void *context)
{
    auto *clock = static_cast<Clock *>(context);
    return clock->origin + clock->consumed / 16;
}
int clockAccept(void *context, const uint8_t *pcm, int bytes)
{
    auto *clock = static_cast<Clock *>(context);
    if (clock->fault)
        return -1;
    if (!clock->running || !pcm || bytes <= 0)
        return 0;
    if ((int)clock->queued.size() + bytes > clock->cap)
        return 0;
    clock->queued.insert(clock->queued.end(), pcm, pcm + bytes);
    return bytes;
}
void clockVolume(void *context, int volume, bool silent)
{
    auto *clock = static_cast<Clock *>(context);
    clock->volume = volume;
    clock->silent = silent;
}
void consume(Clock &clock, int milliseconds)
{
    int bytes = milliseconds * 32;
    if (bytes > (int)clock.queued.size())
        bytes = (int)clock.queued.size();
    clock.consumed += bytes / 2;
    clock.queued.erase(clock.queued.begin(), clock.queued.begin() + bytes);
}

PlaybackMedia message(const char *id, uint32_t duration, uint32_t audio, bool sketch, uint32_t sketch_bytes = 0)
{
    PlaybackMedia media{};
    std::snprintf(media.message_id, sizeof(media.message_id), "%s", id);
    std::snprintf(media.audio_format, sizeof(media.audio_format), "pcm_s16le");
    media.duration_ms = duration;
    media.audio_bytes = audio;
    media.sketch = sketch;
    media.sketch_bytes = sketch_bytes;
    return media;
}

void deliver(PlaybackService &service, const product_inbox_http::Job &job, const uint8_t *data, int length,
             int status = 206)
{
    if (data && length > 0 && job.file_path[0])
        writeFile(job.file_path, data, length, job.range_start == 0);
    product_inbox_http::Result result{};
    result.kind = product_inbox_http::kPlaybackMediaJob;
    result.operation_id = job.operation_id;
    result.generation = job.generation;
    result.perform_ok = status == 200 || status == 206;
    result.http_status = status;
    result.terminal = 1;
    result.hash_matches = 1;
    result.bytes_written = result.perform_ok && length > 0 ? (uint32_t)length : 0;
    result.full_asset = job.asset_bytes > 0 && job.range_start + result.bytes_written >= job.asset_bytes &&
                        result.bytes_written > 0;
    std::snprintf(result.message_id, sizeof(result.message_id), "%s", job.message_id);
    service.onMediaResult(result);
}

void playbackTests()
{
    MediaStore store("playback-host-play");
    store.discardSession();
    DisplaySurfaceBroker broker;
    FetchState fetch;
    g_fetch = &fetch;
    Clock clock;
    PlaybackTransport transport{fetchStart, nullptr, nullptr, nullptr, requestQuiesce, isQuiescent};
    AudioEndpoint endpoint{&clock, clockStart, clockPause, clockStop, clockMs, clockAccept, clockVolume};
    PlaybackService service(store, broker, transport, endpoint);
    service.setMediaSubmit(submitMedia, nullptr);
    service.setSession("u0", 7, "token", true, 40, 1000);
    auto first = message("ari", 100, 3200, true, 24);
    service.bind(&first, true);
    uint8_t pcm[3200];
    std::memset(pcm, 1, sizeof(pcm));
    uint8_t sketch[24];
    putPoint(sketch, 1, 2, 0xF800, 0);
    putPoint(sketch + 8, 4, 5, 0x07E0, 40);
    putPoint(sketch + 16, 9, 9, 0x001F, 90);
    service.toggle();
    assert(service.snapshot().transport == PlaybackSnapshot::Transport::Pause);
    assert(fetch.jobs.size() == 1);
    assert(std::strcmp(fetch.jobs[0].range_header, "bytes=0-3199") == 0);
    deliver(service, fetch.jobs[0], pcm, 3200);
    service.advance(1100);
    assert(clock.starts == 1);
    assert(clock.volume == 40);
    assert(fetch.jobs.size() == 2);
    deliver(service, fetch.jobs[1], sketch, 24);
    consume(clock, 40);
    service.advance(1200);
    SurfaceLease lease{};
    assert(broker.acquire("inbox", SurfacePlace::Host, &lease));
    service.paint(broker.pixels(lease), fl_product_inbox::kSketchPixels);
    assert(service.inkFor("ari"));
    const uint16_t *pixels = broker.pixels(lease);
    assert(pixels[2 * 120 + 1] == 0xF800);
    assert(pixels[5 * 120 + 4] == 0x07E0);
    assert(pixels[9 * 120 + 9] == 0);
    service.pausePreserving();
    assert(!service.snapshot().playing);
    const int paused = service.snapshot().positionMs;
    assert(paused >= 40);
    service.toggle();
    service.advance(1300);
    consume(clock, 70);
    service.advance(1400);
    assert(service.snapshot().transport == PlaybackSnapshot::Transport::Play);
    assert(!service.snapshot().playing);
    service.paint(broker.pixels(lease), fl_product_inbox::kSketchPixels);
    assert(pixels[9 * 120 + 9] == 0x001F);
    service.toggle();
    service.advance(1500);
    service.paint(broker.pixels(lease), fl_product_inbox::kSketchPixels);
    assert(pixels[9 * 120 + 9] == 0);
    assert(service.snapshot().playing);

    auto other = message("sam", 100, 3200, false);
    service.bind(&other, true);
    assert(!service.snapshot().playing);
    assert(service.inkFor("ari"));
    assert(!service.inkFor("sam"));
    product_inbox_http::Result late{};
    late.kind = product_inbox_http::kPlaybackMediaJob;
    late.operation_id = 1;
    late.generation = 7;
    late.perform_ok = 1;
    late.http_status = 200;
    late.terminal = 1;
    late.hash_matches = 1;
    late.bytes_written = 4;
    std::snprintf(late.message_id, sizeof(late.message_id), "ari");
    service.onMediaResult(late);
    service.advance(1600);
    assert(!service.inkFor("sam"));
    assert(!service.snapshot().playing);

    service.bind(&first, true);
    service.paint(broker.pixels(lease), fl_product_inbox::kSketchPixels);
    assert(pixels[2 * 120 + 1] == 0xF800);
    assert(!service.snapshot().playing);
    assert(broker.release(&lease));

    service.setSession("u0", 7, "token", false, 0, 1700);
    assert(!service.snapshot().enabled);
    assert(store.readAudio("ari", 0, pcm, 16) == 0);
    assert(service.inkFor("ari"));
    service.setSession("u0", 7, "token", true, 0, 1800);
    assert(clock.silent);

    service.bind(&first, true);
    service.toggle();
    clock.fault = true;
    service.advance(1900);
    assert(service.snapshot().cantPlay);
    assert(!service.snapshot().playing);
    service.advance(4000);
    assert(!service.snapshot().cantPlay);

    fetch.refuse = true;
    auto third = message("jo", 100, 3200, false);
    service.bind(&third, true);
    const int submits = fetch.submits;
    service.toggle();
    assert(service.snapshot().transport == PlaybackSnapshot::Transport::Pause);
    assert(fetch.submits == submits + 1);
    service.advance(4100);
    fetch.refuse = false;
    service.advance(4200);
    assert(fetch.submits > submits + 1);

    service.requestStop();
    assert(!service.quiesced());
    fetch.ack = true;
    service.advance(4300);
    assert(service.quiesced());
    service.signOut();
    service.setSession("u0", 7, "token", true, 40, 4400);
    late.generation = 7;
    service.onMediaResult(late);
    service.setSession("u0", 8, "next", true, 40, 5000);
    service.advance(5100);
    assert(!service.inkFor("ari"));
    assert(store.stored("ari", MediaKind::Audio) == 0);

    uint8_t diagonal[8 * 6];
    for (int i = 0; i < 6; ++i)
        putPoint(diagonal + i * 8, (uint8_t)i, (uint8_t)i, 0xFFFF, (uint32_t)i);
    uint8_t single[8];
    putPoint(single, 3, 3, 0x0000, 0);
    uint8_t bad[16];
    putPoint(bad, 200, 1, 0xFFFF, 0);
    putPoint(bad + 8, 1, 1, 0xFFFF, 5);
    uint8_t backwards[16];
    putPoint(backwards, 1, 1, 0xFFFF, 10);
    putPoint(backwards + 8, 2, 2, 0xFFFF, 4);
    plant(store, "diag", MediaKind::Audio, 3200, pcm, 3200, true);
    plant(store, "diag", MediaKind::Sketch, (uint32_t)sizeof(diagonal), diagonal, (int)sizeof(diagonal), true);
    auto diag = message("diag", 100, 3200, true, (uint32_t)sizeof(diagonal));
    service.bind(&diag, true);
    service.toggle();
    service.advance(5200);
    consume(clock, 3);
    service.advance(5300);
    assert(broker.acquire("inbox", SurfacePlace::Host, &lease));
    service.paint(broker.pixels(lease), fl_product_inbox::kSketchPixels);
    assert(broker.pixels(lease)[0] == 0xFFFF);
    assert(broker.pixels(lease)[3 * 120 + 3] == 0xFFFF);
    assert(broker.pixels(lease)[1 * 120 + 2] == 0);
    assert(broker.heldCount() == 1);
    assert(broker.release(&lease));

    plant(store, "one", MediaKind::Audio, 3200, pcm, 3200, true);
    plant(store, "one", MediaKind::Sketch, 8, single, 8, true);
    auto one = message("one", 100, 3200, true, 8);
    service.bind(&one, true);
    service.toggle();
    service.advance(5400);
    assert(broker.acquire("inbox", SurfacePlace::Host, &lease));
    service.paint(broker.pixels(lease), fl_product_inbox::kSketchPixels);
    assert(service.inkFor("one"));
    assert(broker.pixels(lease)[3 * 120 + 3] == 0);
    assert(service.snapshot().playing);
    assert(broker.release(&lease));

    plant(store, "bad-sketch", MediaKind::Audio, 3200, pcm, 3200, true);
    plant(store, "bad-sketch", MediaKind::Sketch, 16, bad, 16, true);
    auto broken = message("bad-sketch", 100, 3200, true, 16);
    service.bind(&broken, true);
    service.toggle();
    service.advance(5500);
    assert(broker.acquire("inbox", SurfacePlace::Host, &lease));
    service.paint(broker.pixels(lease), fl_product_inbox::kSketchPixels);
    assert(service.snapshot().playing);
    assert(!service.snapshot().cantPlay);
    assert(broker.release(&lease));

    plant(store, "late", MediaKind::Audio, 3200, pcm, 3200, true);
    plant(store, "late", MediaKind::Sketch, 16, backwards, 16, true);
    auto late_media = message("late", 100, 3200, true, 16);
    service.bind(&late_media, true);
    service.toggle();
    service.advance(5600);
    consume(clock, 20);
    service.advance(5700);
    assert(broker.acquire("inbox", SurfacePlace::Host, &lease));
    service.paint(broker.pixels(lease), fl_product_inbox::kSketchPixels);
    assert(service.snapshot().playing);
    assert(broker.release(&lease));
    service.signOut();
}

std::vector<product_inbox_http::Job> inbox_jobs;
std::deque<product_inbox_http::Result> inbox_done;
bool inboxStart()
{
    return true;
}
bool inboxSubmit(const product_inbox_http::Job *job)
{
    if (!job)
        return false;
    inbox_jobs.push_back(*job);
    return true;
}
const product_inbox_http::Result *inboxPoll()
{
    static product_inbox_http::Result result;
    if (inbox_done.empty())
        return nullptr;
    result = inbox_done.front();
    inbox_done.pop_front();
    return &result;
}
void controllerNavigate(void *context, InboxDestination)
{
    ++*static_cast<int *>(context);
}

void controllerTests(lv_obj_t *content)
{
    using namespace fl_product_inbox;
    inbox_jobs.clear();
    inbox_done.clear();
    MediaStore store("playback-host-card");
    store.discardSession();
    store.setPlaybackAllowed(true);
    DisplaySurfaceBroker broker;
    FetchState fetch;
    g_fetch = &fetch;
    Clock clock;
    PlaybackTransport transport{fetchStart, nullptr, nullptr, nullptr, requestQuiesce, isQuiescent};
    AudioEndpoint endpoint{&clock, clockStart, clockPause, clockStop, clockMs, clockAccept, clockVolume};
    PlaybackService playback(store, broker, transport, endpoint);
    playback.setMediaSubmit(submitMedia, nullptr);
    uint8_t pcm[3200];
    std::memset(pcm, 2, sizeof(pcm));
    uint8_t sketch[8];
    putPoint(sketch, 10, 11, 0xF800, 0);
    plant(store, "m0", MediaKind::Audio, 3200, pcm, 3200, true);
    plant(store, "m0", MediaKind::Sketch, 8, sketch, 8, true);
    InboxService inbox({inboxStart, inboxSubmit, inboxPoll});
    inbox.setSession("u0", "credential", 3, true, true, 0);
    inbox.advance(0);
    auto *header = static_cast<InboxMetadataHeader *>(std::calloc(1, sizeof(InboxMetadataHeader) + sizeof(InboxMessage)));
    header->message_count = 1;
    header->inbox_revision = 1;
    auto *item = reinterpret_cast<InboxMessage *>(header + 1);
    std::strcpy(item->message_id, "m0");
    std::strcpy(item->sender_name, "Ari");
    std::strcpy(item->audio.format_id, "pcm_s16le");
    item->audio.present = 1;
    item->audio.byte_count = 3200;
    item->duration_ms = 100;
    item->sketch.present = 1;
    item->sketch.byte_count = 8;
    std::strcpy(item->sketch.format_id, "flsk2");
    product_inbox_http::Result loaded{};
    loaded.kind = inbox_jobs.back().kind;
    loaded.generation = inbox_jobs.back().generation;
    loaded.operation_id = inbox_jobs.back().operation_id;
    loaded.perform_ok = 1;
    loaded.http_status = 200;
    loaded.terminal = 1;
    loaded.parsed = header;
    inbox_done.push_back(loaded);
    inbox.advance(1);
    playback.setSession("u0", 3, "credential", true, 12, 1);
    RootViewController root(content);
    int navigations = 0;
    auto *view = new InboxViewController(inbox, playback, {&navigations, controllerNavigate});
    assert(root.replace(std::unique_ptr<ViewController>(view), false));
    lv_obj_update_layout(content);
    assert(std::strcmp(lv_label_get_text(view->heading()), "From Ari") == 0);
    assert(std::strcmp(lv_label_get_text(lv_obj_get_child(view->transportButton(), 0)), "Play") == 0);
    auto *card = view->cardForIndex(0);
    lv_obj_send_event(card, LV_EVENT_PRESSED, nullptr);
    lv_obj_send_event(card, LV_EVENT_CLICKED, nullptr);
    assert(!playback.snapshot().playing);
    lv_obj_send_event(view->transportButton(), LV_EVENT_CLICKED, nullptr);
    assert(playback.snapshot().playing);
    assert(std::strcmp(lv_label_get_text(lv_obj_get_child(view->transportButton(), 0)), "Pause") == 0);
    assert(lv_obj_is_hidden(lv_obj_get_child(card, 0)));
    root.dispatchBoot();
    assert(navigations == 1);
    assert(!playback.snapshot().playing);
    assert(playback.inkFor("m0"));
    assert(root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController), false));
    assert(broker.heldCount() == 0);
    auto *returned = new InboxViewController(inbox, playback, {&navigations, controllerNavigate});
    assert(root.replace(std::unique_ptr<ViewController>(returned), false));
    assert(!playback.snapshot().playing);
    assert(playback.inkFor("m0"));
    assert(lv_obj_is_hidden(lv_obj_get_child(returned->cardForIndex(0), 0)));
    playback.signOut();
    store.discardSession();
}
} // namespace

std::vector<product_inbox_http::Result> dispatchReady;
bool dispatchStart()
{
    return true;
}
bool dispatchSubmit(const product_inbox_http::Job *)
{
    return true;
}
const product_inbox_http::Result *dispatchPoll()
{
    static product_inbox_http::Result result;
    if (dispatchReady.empty())
        return nullptr;
    result = dispatchReady.back();
    dispatchReady.pop_back();
    return &result;
}
int releasedBlocks;

void releaseCounted(void *block)
{
    if (!block)
        return;
    ++releasedBlocks;
    std::free(block);
}

struct MediaSinkBox {
    PlaybackService *playback = nullptr;
    int *seen = nullptr;
};
void takeMedia(void *context, const product_inbox_http::Result &result)
{
    auto *box = static_cast<MediaSinkBox *>(context);
    ++*box->seen;
    box->playback->onMediaResult(result);
}

void playback_media_uses_bounded_ranges_and_one_operation_per_asset()
{
    MediaStore store("playback-host-range");
    store.discardSession();
    DisplaySurfaceBroker broker;
    FetchState fetch;
    g_fetch = &fetch;
    PlaybackService service(store, broker);
    service.setMediaSubmit(submitMedia, nullptr);
    service.setSession("u0", 1, "token", true, 0, 1);
    auto clip = message("wide", 10000, 40000, true, 20000);
    service.bind(&clip, true);
    service.toggle();
    assert(fetch.jobs.size() == 1);
    assert(std::strcmp(fetch.jobs[0].range_header, "bytes=0-16383") == 0);
    assert(std::strchr(fetch.jobs[0].range_header, '-') != nullptr);
    assert(fetch.jobs[0].range_header[std::strlen(fetch.jobs[0].range_header) - 1] != '-');
    const uint32_t operation = fetch.jobs[0].operation_id;
    product_inbox_http::Result slice{};
    slice.kind = product_inbox_http::kPlaybackMediaJob;
    slice.operation_id = operation;
    slice.generation = 1;
    slice.perform_ok = 1;
    slice.http_status = 206;
    slice.hash_matches = 1;
    slice.bytes_written = 16384;
    std::snprintf(slice.message_id, sizeof(slice.message_id), "wide");
    service.onMediaResult(slice);
    service.advance(2);
    assert(fetch.jobs.size() == 2);
    assert(fetch.jobs[1].operation_id == operation);
    assert(std::strcmp(fetch.jobs[1].range_header, "bytes=16384-32767") == 0);
    product_inbox_http::Result failed = slice;
    failed.operation_id = fetch.jobs[1].operation_id;
    failed.perform_ok = 0;
    failed.http_status = 500;
    failed.bytes_written = 0;
    service.onMediaResult(failed);
    assert(store.stored("wide", MediaKind::Audio) == 0);
    service.toggle();
    assert(fetch.jobs.back().operation_id != operation);
    assert(std::strcmp(fetch.jobs.back().range_header, "bytes=0-16383") == 0);
}

void playback_results_are_dispatched_not_dropped()
{
    dispatchReady.clear();
    MediaStore store("playback-host-dispatch");
    store.discardSession();
    DisplaySurfaceBroker broker;
    FetchState fetch;
    g_fetch = &fetch;
    PlaybackService playback(store, broker);
    playback.setMediaSubmit(submitMedia, nullptr);
    playback.setSession("u0", 4, "token", true, 0, 10);
    auto clip = message("disp", 100, 100, false);
    playback.bind(&clip, true);
    playback.toggle();
    assert(fetch.jobs.size() == 1);
    product_inbox_http::Result done{};
    done.kind = product_inbox_http::kPlaybackMediaJob;
    done.operation_id = fetch.jobs[0].operation_id;
    done.generation = 4;
    done.perform_ok = 1;
    done.http_status = 206;
    done.hash_matches = 1;
    done.full_asset = 1;
    done.bytes_written = 100;
    std::snprintf(done.message_id, sizeof(done.message_id), "disp");
    dispatchReady.push_back(done);
    InboxService inbox({dispatchStart, dispatchSubmit, dispatchPoll});
    int seen = 0;
    MediaSinkBox box{&playback, &seen};
    inbox.setMediaSink(takeMedia, &box);
    inbox.setSession("u0", "token", 4, true, true, 10);
    inbox.advance(10);
    assert(seen == 1);
    assert(store.complete("disp", MediaKind::Audio));
    assert(dispatchReady.empty());
}

void unknown_result_kinds_free_data()
{
    dispatchReady.clear();
    releasedBlocks = 0;
    InboxService inbox({dispatchStart, dispatchSubmit, dispatchPoll});
    inbox.setResultReleaser(releaseCounted);
    inbox.setSession("u0", "token", 1, true, true, 1);
    product_inbox_http::Result odd{};
    odd.kind = 99;
    odd.data = static_cast<uint8_t *>(std::malloc(8));
    odd.parsed = std::malloc(8);
    dispatchReady.push_back(odd);
    inbox.advance(1);
    assert(releasedBlocks == 2);
    assert(dispatchReady.empty());
}

void testPlaybackService(lv_obj_t *content)
{
    surfaceTests();
    mediaTests();
    playbackTests();
    playback_media_uses_bounded_ranges_and_one_operation_per_asset();
    playback_results_are_dispatched_not_dropped();
    unknown_result_kinds_free_data();
    controllerTests(content);
}
