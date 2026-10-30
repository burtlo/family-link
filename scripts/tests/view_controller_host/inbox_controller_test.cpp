#include "product_inbox_view_controller.hpp"
#include "product_playback_service.hpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

void advancePinTestClock(uint32_t milliseconds);

namespace
{
using namespace product_ui;
using namespace product_inbox_http;
using namespace fl_product_inbox;
std::vector<Job> jobs;
std::deque<Result> completions;
bool refuse;
bool fakeStart()
{
    return true;
}
bool fakeSubmit(const Job *job)
{
    assert(job->kind == kMetadataJob || job->kind == 2);
    assert(std::strstr(job->path, "/media/") == nullptr);
    if (refuse)
        return false;
    jobs.push_back(*job);
    return true;
}
const Result *fakePoll()
{
    static Result result;
    if (completions.empty())
        return nullptr;
    result = completions.front();
    completions.pop_front();
    return &result;
}
int countJobs(int kind)
{
    int count = 0;
    for (const auto &job : jobs)
        if (job.kind == kind)
            ++count;
    return count;
}
Job latest(int kind)
{
    for (auto it = jobs.rbegin(); it != jobs.rend(); ++it)
        if (it->kind == kind)
            return *it;
    assert(false);
    return {};
}
InboxMetadataHeader *block(int count, unsigned revision, const char *last = "", int base = 0)
{
    auto *header = static_cast<InboxMetadataHeader *>(std::calloc(1, sizeof(InboxMetadataHeader) + count * sizeof(InboxMessage)));
    header->message_count = count;
    header->inbox_revision = revision;
    std::snprintf(header->last_viewed_message_id, sizeof(header->last_viewed_message_id), "%s", last);
    auto *messages = reinterpret_cast<InboxMessage *>(header + 1);
    for (int i = 0; i < count; ++i) {
        std::snprintf(messages[i].message_id, sizeof(messages[i].message_id), "m%d", i + base);
        std::snprintf(messages[i].sender_name, sizeof(messages[i].sender_name), "Sender %d", i + base);
        std::strcpy(messages[i].sender_card_background_id, "card-bg-02");
        messages[i].duration_ms = (i + 1) * 1000;
        messages[i].audio.present = 1;
    }
    return header;
}
void respond(const Job &job, InboxMetadataHeader *header, int status = 200, bool perform = true)
{
    Result result{};
    result.kind = job.kind;
    result.generation = job.generation;
    result.operation_id = job.operation_id;
    result.http_status = status;
    result.perform_ok = perform;
    result.terminal = 1;
    result.parsed = header;
    completions.push_back(result);
}
void refresh(InboxService &service, InboxMetadataHeader *header, int64_t now)
{
    service.requestRefresh();
    service.advance(now);
    respond(latest(kMetadataJob), header);
    service.advance(now + 1);
}
struct Navigation {
    int count = 0;
    InboxDestination destination = InboxDestination::Settings;
};
void navigate(void *context, InboxDestination destination)
{
    auto *nav = static_cast<Navigation *>(context);
    ++nav->count;
    nav->destination = destination;
}
void serviceTests()
{
    jobs.clear();
    completions.clear();
    refuse = false;
    InboxService service({fakeStart, fakeSubmit, fakePoll});
    service.setSession("u0", "credential", 1, true, true, 0);
    service.advance(0);
    const Job first = latest(kMetadataJob);
    assert(std::strcmp(first.bearer, "credential") == 0 && first.timeout_ms == 6000);
    respond(first, block(6, 1, "m1"));
    service.advance(1);
    assert(service.snapshot().body == InboxSnapshot::Body::Cards);
    assert(service.snapshot().selectedIndex == 1);
    service.advance(500);
    assert(service.selectIndex(4, "m4"));
    assert(!service.selectIndex(4, "m3"));
    service.advance(1001);
    assert(countJobs(2) == 0);
    service.advance(1500);
    const Job write = latest(2);
    assert(std::strcmp(write.message_id, "m4") == 0);
    respond(write, nullptr, 0, false);
    service.advance(1501);
    service.advance(2501);
    const Job retry = latest(2);
    assert(std::strcmp(retry.body, write.body) == 0);
    assert(retry.operation_id != write.operation_id);
    assert(service.selectIndex(2, "m2"));
    respond(retry, nullptr);
    service.advance(2502);
    service.advance(3501);
    const Job newWrite = latest(2);
    assert(std::strcmp(newWrite.message_id, "m2") == 0);
    assert(std::strcmp(newWrite.body, write.body) != 0);
    assert(service.snapshot().selectedIndex == 2);
    respond(write, nullptr); // Late prior attempt cannot overwrite the active write or selection.
    service.advance(3502);
    assert(service.snapshot().selectedIndex == 2);
    respond(newWrite, nullptr);
    service.advance(3503);
    refresh(service, block(7, 2, "m6"), 4000);
    assert(std::strcmp(service.selectedMessageId(), "m2") == 0);
    // A snapshot from another endpoint does not steal the current selection.
    auto *removed = block(6, 3);
    auto *items = reinterpret_cast<InboxMessage *>(removed + 1);
    for (int i = 2; i < 6; ++i) {
        std::snprintf(items[i].message_id, sizeof(items[i].message_id), "m%d", i + 1);
    }
    refresh(service, removed, 4100);
    assert(std::strcmp(service.selectedMessageId(), "m1") == 0);
    refresh(service, block(5, 4, "m5", 2), 4200);
    assert(std::strcmp(service.selectedMessageId(), "m3") == 0); // No older survives: next newer retained.
    refresh(service, block(80, 5, "m79"), 4300);
    assert(service.snapshot().count == 80 && std::strcmp(service.selectedMessageId(), "m3") == 0);
    assert(service.selectIndex(79, "m79"));
    refresh(service, block(1, 4), 4400); // Lower revision is ignored without hiding the current carousel.
    assert(service.snapshot().count == 80 && service.snapshot().body == InboxSnapshot::Body::Cards);
    service.setSession("u0", "credential", 1, false, true, 4500);
    assert(service.snapshot().body == InboxSnapshot::Body::NoWifi);
    assert(!service.selectIndex(0));
    service.setSession("u0", "credential", 1, true, true, 4600);
    assert(service.snapshot().body == InboxSnapshot::Body::Connecting);
    service.advance(4600);
    respond(latest(kMetadataJob), block(80, 6));
    service.advance(4601);
    assert(std::strcmp(service.selectedMessageId(), "m79") == 0);
    service.requestRefresh();
    service.advance(4700);
    Job stale = latest(kMetadataJob);
    service.signOut();
    service.setSession("other", "new-credential", 2, true, true, 4800);
    service.advance(4800);
    const Job fresh = latest(kMetadataJob);
    respond(stale, block(80, 99));
    respond(fresh, block(1, 1, "m0"));
    service.advance(4801);
    assert(service.snapshot().count == 1 && service.snapshot().selectedIndex == 0);
    // Empty and first arrival are valid, and no service work needs a loaded controller.
    refresh(service, block(0, 2), 4900);
    assert(service.snapshot().body == InboxSnapshot::Body::Empty);
    refresh(service, block(1, 3), 5000);
    assert(service.snapshot().selectedIndex == 0);
    refuse = true;
    service.requestRefresh();
    int count = countJobs(kMetadataJob);
    service.advance(5100);
    assert(countJobs(kMetadataJob) == count);
    refuse = false;
    service.advance(6100);
    assert(countJobs(kMetadataJob) == count + 1);
    respond(latest(kMetadataJob), nullptr, 500);
    service.advance(6101);
    assert(service.snapshot().body == InboxSnapshot::Body::Connecting);
    service.advance(7101);
    assert(latest(kMetadataJob).timeout_ms == 6000);
    respond(latest(kMetadataJob), block(1, 4));
    service.advance(7102);
    assert(service.snapshot().body == InboxSnapshot::Body::Cards);
    service.signOut();
    service.advance(8000);
    assert(service.snapshot().count == 0);
}

std::string json(int count)
{
    std::string body = "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":1,\"total_count\":" +
                       std::to_string(count) + ",\"last_viewed_message_id\":\"m0\",\"next_cursor\":null,\"has_more\":false,\"items\":[";
    for (int i = 0; i < count; ++i) {
        if (i)
            body += ',';
        body += "{\"message_id\":\"m" + std::to_string(i) + "\",\"sender_user_id\":\"u0\",\"sender_name\":\"Person\",\"kind\":\"audio\",\"duration_ms\":1234,\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":32000,\"sha256\":\"" + std::string(64, 'a') + "\"}}}";
    }
    return body + "]}";
}
void parserTests()
{
    InboxMetadataHeader header{};
    const auto body = json(80);
    int count = 0;
    auto visitor = [](void *context, const InboxMessage &message) {
        auto *count = static_cast<int *>(context);
        assert(message.audio.present && std::strcmp(message.sender_name, "Person") == 0);
        ++*count;
        return true;
    };
    assert(parse_inbox_metadata(body.data(), body.size(), body.size(), &header, visitor, &count));
    assert(count == 80 && header.message_count == 80);
    auto legacy = std::unique_ptr<InboxParseResult>(new InboxParseResult);
    assert(!parse_inbox_snapshot(body.data(), body.size(), body.size(), legacy.get()) && legacy->overflow);
    std::string invalid = body.substr(0, body.size() - 2);
    assert(!parse_inbox_metadata(invalid.data(), invalid.size(), invalid.size(), &header));
    invalid = json(1);
    const auto position = invalid.find("false");
    invalid.replace(position, 5, "true");
    assert(!parse_inbox_metadata(invalid.data(), invalid.size(), invalid.size(), &header));
    assert(!parse_inbox_metadata(body.data(), body.size(), 262145, &header));
    const auto empty = json(0);
    assert(parse_inbox_metadata(empty.data(), empty.size(), empty.size(), &header) && header.message_count == 0);
    std::string nullable = json(1);
    const auto avatar = nullable.find("\"sender_avatar_id\":\"u0\"");
    // The live desk projects a missing catalog avatar as JSON null, not an omitted key.
    if (avatar == std::string::npos) {
        const auto named = nullable.find("\"sender_name\":\"Person\"");
        nullable.insert(named, "\"sender_avatar_id\":null,");
    } else {
        nullable.replace(avatar, std::strlen("\"sender_avatar_id\":\"u0\""), "\"sender_avatar_id\":null");
    }
    assert(parse_inbox_metadata(nullable.data(), nullable.size(), nullable.size(), &header) &&
           header.message_count == 1);
}
} // namespace

void testInboxControllers(lv_obj_t *content)
{
    serviceTests();
    parserTests();
    jobs.clear();
    completions.clear();
    refuse = false;
    MediaStore media("playback-host-inbox");
    media.discardSession();
    DisplaySurfaceBroker broker;
    PlaybackService playback(media, broker);
    InboxService service({fakeStart, fakeSubmit, fakePoll});
    service.setSession("u0", "credential", 1, true, true, 0);
    service.advance(0);
    respond(latest(kMetadataJob), block(80, 1));
    service.advance(1);
    Navigation nav;
    {
        RootViewController root(content);
        auto *inbox = new InboxViewController(service, playback, {&nav, navigate});
        assert(root.replace(std::unique_ptr<ViewController>(inbox), false));
        lv_obj_update_layout(content);
        assert(std::strcmp(lv_label_get_text(inbox->heading()), "From Sender 79") == 0);
        assert(lv_obj_get_scroll_x(inbox->carouselRow()) == 79 * 144);
        assert(inbox->cardForIndex(79));
        const auto children = lv_obj_get_child_count(lv_obj_get_parent(inbox->cardForIndex(79)));
        assert(children == 3);
        for (int index = 78; index >= 0; --index) {
            lv_obj_scroll_to_x(inbox->carouselRow(), index * 144, LV_ANIM_OFF);
            lv_obj_send_event(inbox->carouselRow(), LV_EVENT_SCROLL_END, nullptr);
            assert(service.snapshot().selectedIndex == index);
            assert(inbox->cardForIndex(index));
        }
        assert(lv_obj_get_scroll_x(inbox->carouselRow()) == 0);
        assert(!inbox->selectMessage(0, "stale"));
        assert(!inbox->selectMessage(80, "m80"));
        auto *peek = inbox->cardForIndex(1);
        lv_obj_send_event(peek, LV_EVENT_PRESSED, nullptr);
        lv_obj_send_event(peek, LV_EVENT_CLICKED, nullptr);
        assert(service.snapshot().selectedIndex == 1);
        lv_obj_send_event(inbox->cardForIndex(1), LV_EVENT_PRESSED, nullptr);
        lv_obj_scroll_to_x(inbox->carouselRow(), 2 * 144, LV_ANIM_OFF);
        lv_obj_send_event(inbox->carouselRow(), LV_EVENT_SCROLL, nullptr);
        lv_obj_send_event(inbox->carouselRow(), LV_EVENT_SCROLL_END, nullptr);
        assert(service.snapshot().selectedIndex == 2);
        // A click at the end of a drag never undoes the scroll selection.
        if (auto *card = inbox->cardForIndex(1))
            lv_obj_send_event(card, LV_EVENT_CLICKED, nullptr);
        assert(service.snapshot().selectedIndex == 2);
        const auto selected = service.selectedMessageId();
        const std::string selectedCopy = selected;
        for (int i = 0; i < 20; ++i)
            root.render();
        assert(std::strcmp(service.selectedMessageId(), selectedCopy.c_str()) == 0);
        root.dispatchBoot();
        assert(nav.count == 1 && nav.destination == InboxDestination::Settings);
        root.dispatchRed();
        assert(nav.count == 1); // One navigation request per appeared instance.
        assert(root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController), false));
        // Service applies updates after controller teardown.
        refresh(service, block(81, 2, "m80"), 500);
        auto *returned = new InboxViewController(service, playback, {&nav, navigate});
        assert(root.replace(std::unique_ptr<ViewController>(returned), false));
        assert(service.snapshot().selectedIndex == 2);
        assert(lv_obj_get_scroll_x(returned->carouselRow()) == 2 * 144);
        root.dispatchRed();
        assert(nav.destination == InboxDestination::MessageTarget);
    }
    service.signOut();
    playback.signOut();
}
