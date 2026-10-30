#include "product_delivery_service.hpp"
#include "product_sketch_view_controller.hpp"
#include "product_view_controller.hpp"

#include "fl_product_record_lab10.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>

namespace
{
using namespace product_ui;

const char *kPath = "record-send-test.bin";

MessageTargetSnapshot jordan()
{
    MessageTargetSnapshot snapshot;
    snapshot.count = 1;
    std::strcpy(snapshot.users[0].id, "jordan");
    std::strcpy(snapshot.users[0].display_name, "Jordan");
    snapshot.selected.count = 1;
    std::strcpy(snapshot.selected.ids[0], "jordan");
    return snapshot;
}

void beginWipe(DeliveryService &delivery, int64_t now)
{
    delivery.setSession("ada", "token", true, false, 1, now);
    if (delivery.place() == fl_product_send::kSendPlaceInbox) {
        auto entered = delivery.apply(fl_product_send::kSendIntentRed, now, true);
        assert(!entered.ignored && delivery.place() == fl_product_send::kSendPlacePicker);
    }
    delivery.mirrorSelection(jordan());
    auto wiping = delivery.apply(fl_product_send::kSendIntentRed, now, true);
    assert(!wiping.ignored && delivery.showsSketch());
}
} // namespace

void testRecordSend(lv_obj_t *content)
{
    std::remove(kPath);
    DisplaySurfaceBroker broker;
    SurfaceLease record{};
    assert(broker.acquireRecord("sketch", SurfacePlace::Host, &record));
    assert(record.bytes == DisplaySurfaceBroker::kRecordBytes && record.width == 240 && record.record);
    assert(broker.heldCount() == 1);
    assert(broker.release(&record) && broker.heldCount() == 0);
    assert(broker.acquireRecord("sketch", SurfacePlace::Host, &record));
    assert(broker.release(&record));

    char resumeBody[64];
    int64_t resumeNow = 0;
    DisplaySurfaceBroker failing;
    failing.setAllocator(
        [](size_t bytes, SurfacePlace, SurfacePlace *, void *) -> void * {
            return bytes >= static_cast<size_t>(DisplaySurfaceBroker::kRecordBytes) ? nullptr : std::malloc(bytes);
        },
        [](void *block, void *) { std::free(block); }, nullptr);
    {
        CaptureService capture;
        assert(capture.open(kPath));
        DeliveryService delivery(capture);
        RootViewController root(content);
        assert(!root.replace(std::unique_ptr<ViewController>(new SketchViewController(
                                 delivery, failing, {})),
                             false));
        assert(failing.heldCount() == 0);

        int64_t now = 1000;
        capture.setMicAvailable(false);
        delivery.setSession("ada", "token", true, false, 1, now);
        assert(!delivery.apply(fl_product_send::kSendIntentRed, now, true).ignored);
        delivery.mirrorSelection(jordan());
        assert(!delivery.apply(fl_product_send::kSendIntentRed, now, true).ignored);
        assert(delivery.couldntRecord());
        char body[64];
        delivery.body(body, static_cast<int>(sizeof(body)));
        assert(std::strcmp(body, "couldn't record") == 0);
        delivery.advance(now + 2000);
        now += 2000;
        assert(delivery.place() == fl_product_send::kSendPlacePicker);

        capture.setMicAvailable(true);
        beginWipe(delivery, now);
        (void)delivery.apply(fl_product_send::kSendIntentBoot, now, true);
        assert(!delivery.showsSketch() && !capture.blocksInboxFetch());
        beginWipe(delivery, now);
        (void)delivery.apply(fl_product_send::kSendIntentMute, now, true);
        assert(delivery.place() == fl_product_send::kSendPlaceInbox);
        beginWipe(delivery, now);
        assert(!capture.inputOpen());
        assert(!capture.routeTouch(20, 20, 0));
        assert(!capture.pushPcm(reinterpret_cast<const uint8_t *>("pcm-before-wipe"), 16));
        delivery.advance(now + 600);
        now += 600;
        assert(delivery.sketchRed() && capture.inputOpen());
        uint8_t pcm[640];
        std::memset(pcm, 0x11, sizeof(pcm));
        assert(capture.pushPcm(pcm, sizeof(pcm)));
        assert(capture.routeTouch(30, 40, 0));
        assert(capture.routeTouch(32, 42, 1));
        assert(capture.routeTouch(32, 42, 2));
        assert(root.replace(std::unique_ptr<ViewController>(new SketchViewController(delivery, broker, {})), false));
        auto *sketch = static_cast<SketchViewController *>(root.current());
        lv_obj_update_layout(sketch->view());
        assert(sketch->canvas() && lv_obj_get_width(sketch->canvas()) == 240);
        const auto *white = lab10_swatch_geometry(LAB10_COLOR_WHITE);
        auto *swatch = sketch->swatch(LAB10_COLOR_WHITE);
        assert(swatch && lv_obj_get_width(swatch) == LAB10_SWATCH_DIAMETER);
        assert(lv_obj_get_x(swatch) == white->center_x - LAB10_RAIL_X - LAB10_SWATCH_RADIUS);
        assert(lv_obj_get_style_outline_width(swatch, LV_PART_MAIN) == 2);
        root.clear();
        assert(broker.heldCount() == 0);
        auto finished = delivery.apply(fl_product_send::kSendIntentRed, now, true);
        assert(!finished.ignored && delivery.showsSketch() && !delivery.sketchRed());
        uint32_t audio = 0;
        uint32_t sketchBytes = 0;
        assert(capture.finishedInfo(delivery.capture().finishedSlot(0)->client_message_id, &audio, &sketchBytes));
        assert(audio >= sizeof(pcm));
        delivery.advance(now + 600);
        now += 600;
        assert(delivery.showsFeedback());
        delivery.body(body, static_cast<int>(sizeof(body)));
        assert(std::strncmp(body, "preparing", 9) == 0);
        assert(std::strstr(body, "sent") == nullptr);
        char title[64];
        char subtitle[96];
        delivery.header(title, static_cast<int>(sizeof(title)), subtitle, static_cast<int>(sizeof(subtitle)));
        assert(std::strcmp(title, "sending to ...") == 0);
        assert(std::strcmp(subtitle, "Jordan") == 0);
        const char *kept = delivery.capture().finishedSlot(0) ? delivery.capture().finishedSlot(0)->client_message_id : "";
        char keptId[48];
        std::snprintf(keptId, sizeof(keptId), "%s", kept);
        fl_product_record::SendIdentity identity{};
        assert(capture.identityFor(keptId, &identity));
        assert(std::strcmp(identity.sender_user_id, "ada") == 0);
        assert(identity.recipient_count == 1 && std::strcmp(identity.recipient_ids[0], "jordan") == 0);
        std::snprintf(resumeBody, sizeof(resumeBody), "%s", body);
        resumeNow = now;
    }

    {
        CaptureService resumed;
        assert(resumed.open(kPath));
        DeliveryService again(resumed);
        again.setSession("other", "token", true, false, 2, resumeNow);
        again.body(resumeBody, static_cast<int>(sizeof(resumeBody)));
        assert(std::strstr(resumeBody, "preparing") == nullptr);
        again.setSession("ada", "token", true, false, 3, resumeNow);
        again.body(resumeBody, static_cast<int>(sizeof(resumeBody)));
        assert(std::strncmp(resumeBody, "preparing", 9) == 0);
    }
    std::remove(kPath);
}
