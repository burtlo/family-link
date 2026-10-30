#include "product_inbox_view_controller.hpp"
#include "product_playback_service.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
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

void mediaTests()
{
    MediaStore store("playback-host-media");
    store.discardSession();
    store.setPlaybackAllowed(true);
    const char *sha = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    assert(store.begin("m1", MediaKind::Audio, 3, sha));
    assert(store.append("m1", MediaKind::Audio, 0, reinterpret_cast<const uint8_t *>("ab"), 2) == MediaStore::Append::Ok);
    store.interrupt("m1", MediaKind::Audio);
    assert(!store.complete("m1", MediaKind::Audio));
    assert(store.stored("m1", MediaKind::Audio) == 2);
    assert(store.append("m1", MediaKind::Audio, 2, reinterpret_cast<const uint8_t *>("c"), 1) ==
           MediaStore::Append::Complete);
    uint8_t big[8000];
    assert(store.readAudio("m1", 0, big, 8000) == 3);
    assert(std::memcmp(big, "abc", 3) == 0);
    store.setPlaybackAllowed(false);
    assert(store.readAudio("m1", 0, big, 8) == 0);
    assert(store.stored("m1", MediaKind::Audio) == 3);
    store.setPlaybackAllowed(true);
    assert(store.begin("bad", MediaKind::Sketch, 3, sha));
    assert(store.append("bad", MediaKind::Sketch, 0, reinterpret_cast<const uint8_t *>("zzz"), 3) ==
           MediaStore::Append::HashMismatch);
    assert(store.failed("bad", MediaKind::Sketch));
    assert(store.readSketch("bad", 0, big, 8) == 3);
    store.discardSession();
    assert(store.stored("m1", MediaKind::Audio) == 0);
}

struct FetchState {
    std::vector<PlaybackFetchJob> jobs;
    std::deque<PlaybackFetchResult> ready;
    bool refuse = false;
    bool ack = false;
    int submits = 0;
};
bool fetchStart()
{
    return true;
}
bool fetchSubmit(const PlaybackFetchJob *job)
{
    auto *state = static_cast<FetchState *>(job ? nullptr : nullptr);
    (void)state;
    return false;
}
FetchState *g_fetch;
bool submitJob(const PlaybackFetchJob *job)
{
    ++g_fetch->submits;
    if (g_fetch->refuse || !job)
        return false;
    g_fetch->jobs.push_back(*job);
    return true;
}
const PlaybackFetchResult *pollJob()
{
    static PlaybackFetchResult result;
    if (g_fetch->ready.empty())
        return nullptr;
    result = g_fetch->ready.front();
    g_fetch->ready.pop_front();
    return &result;
}
void releaseJob(const PlaybackFetchResult *result)
{
    if (result && result->data)
        std::free(result->data);
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

void deliver(const PlaybackFetchJob &job, const uint8_t *data, int length, int status = 206)
{
    PlaybackFetchResult result{};
    result.operation = job.operation;
    result.generation = job.generation;
    result.epoch = job.epoch;
    result.kind = job.kind;
    result.perform_ok = status == 200 || status == 206;
    result.http_status = status;
    result.terminal = 1;
    std::snprintf(result.message_id, sizeof(result.message_id), "%s", job.message_id);
    if (data && length > 0) {
        result.data = static_cast<uint8_t *>(std::malloc((size_t)length));
        std::memcpy(result.data, data, (size_t)length);
        result.data_len = length;
    }
    g_fetch->ready.push_back(result);
}

void playbackTests()
{
    MediaStore store("playback-host-play");
    store.discardSession();
    DisplaySurfaceBroker broker;
    FetchState fetch;
    g_fetch = &fetch;
    Clock clock;
    PlaybackTransport transport{fetchStart, submitJob, pollJob, releaseJob, requestQuiesce, isQuiescent};
    AudioEndpoint endpoint{&clock, clockStart, clockPause, clockStop, clockMs, clockAccept, clockVolume};
    PlaybackService service(store, broker, transport, endpoint);
    service.setSession(7, "token", true, 40, 1000);
    auto first = message("ari", 100, 3200, true, 24);
    service.bind(&first, true);
    uint8_t pcm[3200];
    std::memset(pcm, 1, sizeof(pcm));
    uint8_t sketch[24];
    putPoint(sketch, 1, 2, 0xF800, 0);
    putPoint(sketch + 8, 4, 5, 0x07E0, 40);
    putPoint(sketch + 16, 9, 9, 0x001F, 90);
    assert(store.begin("ari", MediaKind::Audio, 3200, ""));
    assert(store.begin("ari", MediaKind::Sketch, 24, ""));
    service.toggle();
    assert(service.snapshot().transport == PlaybackSnapshot::Transport::Pause);
    assert(fetch.jobs.size() == 2);
    deliver(fetch.jobs[0], pcm, 3200);
    deliver(fetch.jobs[1], sketch, 24);
    service.advance(1100);
    assert(clock.starts == 1);
    assert(clock.volume == 40);
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
    PlaybackFetchResult late = fetch.ready.empty() ? PlaybackFetchResult{} : fetch.ready.front();
    late.operation = 1;
    late.generation = 7;
    late.epoch = 1;
    late.kind = PlaybackService::kAudioKind;
    late.perform_ok = 1;
    late.http_status = 200;
    late.terminal = 1;
    std::snprintf(late.message_id, sizeof(late.message_id), "ari");
    late.data = static_cast<uint8_t *>(std::malloc(4));
    late.data_len = 4;
    fetch.ready.push_back(late);
    service.advance(1600);
    assert(!service.inkFor("sam"));
    assert(!service.snapshot().playing);

    service.bind(&first, true);
    service.paint(broker.pixels(lease), fl_product_inbox::kSketchPixels);
    assert(pixels[2 * 120 + 1] == 0xF800);
    assert(!service.snapshot().playing);
    assert(broker.release(&lease));

    service.setSession(7, "token", false, 0, 1700);
    assert(!service.snapshot().enabled);
    assert(store.readAudio("ari", 0, pcm, 16) == 0);
    assert(service.inkFor("ari"));
    service.setSession(7, "token", true, 0, 1800);
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
    service.setSession(7, "token", true, 40, 4400);
    late.generation = 7;
    late.data = static_cast<uint8_t *>(std::malloc(4));
    late.data_len = 4;
    fetch.ready.push_back(late);
    service.setSession(8, "next", true, 40, 5000);
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
    assert(store.begin("diag", MediaKind::Audio, 3200, ""));
    assert(store.append("diag", MediaKind::Audio, 0, pcm, 3200) == MediaStore::Append::Complete);
    assert(store.begin("diag", MediaKind::Sketch, sizeof(diagonal), ""));
    assert(store.append("diag", MediaKind::Sketch, 0, diagonal, (int)sizeof(diagonal)) == MediaStore::Append::Complete);
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

    assert(store.begin("one", MediaKind::Audio, 3200, ""));
    assert(store.append("one", MediaKind::Audio, 0, pcm, 3200) == MediaStore::Append::Complete);
    assert(store.begin("one", MediaKind::Sketch, 8, ""));
    assert(store.append("one", MediaKind::Sketch, 0, single, 8) == MediaStore::Append::Complete);
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

    assert(store.begin("bad-sketch", MediaKind::Audio, 3200, ""));
    assert(store.append("bad-sketch", MediaKind::Audio, 0, pcm, 3200) == MediaStore::Append::Complete);
    assert(store.begin("bad-sketch", MediaKind::Sketch, 16, ""));
    assert(store.append("bad-sketch", MediaKind::Sketch, 0, bad, 16) == MediaStore::Append::Complete);
    auto broken = message("bad-sketch", 100, 3200, true, 16);
    service.bind(&broken, true);
    service.toggle();
    service.advance(5500);
    assert(broker.acquire("inbox", SurfacePlace::Host, &lease));
    service.paint(broker.pixels(lease), fl_product_inbox::kSketchPixels);
    assert(service.snapshot().playing);
    assert(!service.snapshot().cantPlay);
    assert(broker.release(&lease));

    assert(store.begin("late", MediaKind::Audio, 3200, ""));
    assert(store.append("late", MediaKind::Audio, 0, pcm, 3200) == MediaStore::Append::Complete);
    assert(store.begin("late", MediaKind::Sketch, 16, ""));
    assert(store.append("late", MediaKind::Sketch, 0, backwards, 16) == MediaStore::Append::Complete);
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
    PlaybackTransport transport{fetchStart, submitJob, pollJob, releaseJob, requestQuiesce, isQuiescent};
    AudioEndpoint endpoint{&clock, clockStart, clockPause, clockStop, clockMs, clockAccept, clockVolume};
    PlaybackService playback(store, broker, transport, endpoint);
    uint8_t pcm[3200];
    std::memset(pcm, 2, sizeof(pcm));
    uint8_t sketch[8];
    putPoint(sketch, 10, 11, 0xF800, 0);
    assert(store.begin("m0", MediaKind::Audio, 3200, ""));
    assert(store.append("m0", MediaKind::Audio, 0, pcm, 3200) == MediaStore::Append::Complete);
    assert(store.begin("m0", MediaKind::Sketch, 8, ""));
    assert(store.append("m0", MediaKind::Sketch, 0, sketch, 8) == MediaStore::Append::Complete);
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
    playback.setSession(3, "credential", true, 12, 1);
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

void testPlaybackService(lv_obj_t *content)
{
    surfaceTests();
    mediaTests();
    playbackTests();
    controllerTests(content);
}
