#include "product_delivery_service.hpp"

#include <cstdio>
#include <cstring>

namespace product_ui
{
namespace
{
void copyText(char *out, int cap, const char *src)
{
    if (!out || cap <= 0)
        return;
    std::snprintf(out, static_cast<size_t>(cap), "%s", src ? src : "");
}
} // namespace

DeliveryService::DeliveryService(CaptureService &capture) : capture_(capture)
{
    fillHooks();
}

void DeliveryService::fillHooks()
{
    std::memset(&hooks_, 0, sizeof(hooks_));
    hooks_.playback = this;
    hooks_.pause_for_record = pausePlayback;
    hooks_.selected_message_id = selectedMessage;
    hooks_.capture = this;
    hooks_.can_start_second_recording = canStartSecond;
    hooks_.discard_in_progress = discardCapture;
    hooks_.cancel_recording = cancelCapture;
    hooks_.finish_take = finishTake;
    hooks_.capture_can_start = captureCanStart;
    hooks_.begin_wipe = beginWipe;
    hooks_.begin_reverse = beginReverse;
    hooks_.begin_finish_collapse = beginCollapse;
}

void DeliveryService::setTransport(DeliveryTransport transport)
{
    transport_ = transport;
}

void DeliveryService::setPlayback(void (*pause)(void *), const char *(*selected)(void *), void *context)
{
    pause_ = pause;
    selected_ = selected;
    playback_ = context;
}

void DeliveryService::setSession(const char *user, const char *bearer, bool wifi, bool refreshFailed,
                                 uint32_t generation, int64_t now)
{
    now_ = now;
    wifi_ = wifi;
    refresh_failed_ = refreshFailed;
    copyText(bearer_, static_cast<int>(sizeof(bearer_)), bearer);
    journey_.set_wifi(wifi ? 1 : 0);
    journey_.set_refresh_failed(refreshFailed ? 1 : 0);
    journey_.set_session_generation(generation);
    const bool same = user && user_[0] && std::strcmp(user_, user) == 0;
    if (!user || !user[0]) {
        user_[0] = 0;
        journey_.set_signed_in_user(nullptr);
        return;
    }
    if (journey_.outbox().client_message_id()[0])
        journey_.on_same_user_signed_in(user);
    else if (journey_.mute_held_finished() && std::strcmp(user_, user) == 0)
        journey_.on_same_user_signed_in(user);
    else
        journey_.set_signed_in_user(user);
    copyText(user_, static_cast<int>(sizeof(user_)), user);
    if (!same)
        adoptFinished(now);
}

void DeliveryService::noteSeed(uint32_t seed)
{
    if (seeded_ || seed == 0)
        return;
    journey_.seed_capture_ids(seed);
    seeded_ = true;
}

void DeliveryService::mirrorSelection(const MessageTargetSnapshot &snapshot)
{
    fl_product_send::HangoutMember members[fl_product_send::kMaxRecipients]{};
    int count = 0;
    for (int s = 0; s < snapshot.selected.count && count < fl_product_send::kMaxRecipients; ++s) {
        const fl_product_login::UserId *user = nullptr;
        for (int i = 0; i < snapshot.count; ++i)
            if (std::strcmp(snapshot.users[i].id, snapshot.selected.ids[s]) == 0)
                user = &snapshot.users[i];
        if (!user)
            continue;
        copyText(members[count].user_id, fl_product_send::kMaxUserId, user->id);
        copyText(members[count].display_name, fl_product_send::kMaxName, user->display_name);
        copyText(members[count].avatar_id, fl_product_send::kMaxAvatarId, user->avatar_id);
        copyText(members[count].card_background_id, fl_product_send::kMaxCardBgId, user->card_background_id);
        ++count;
    }
    (void)journey_.picker().load_members(members, count, user_);
    const int loaded = journey_.picker().member_count();
    for (int i = 0; i < loaded; ++i)
        journey_.picker().toggle_selection(i);
}

fl_product_send::SendStageResult DeliveryService::apply(fl_product_send::SendIntent intent, int64_t now, bool signedIn)
{
    now_ = now;
    if (intent == fl_product_send::kSendIntentRed && stage_.place() == fl_product_send::kSendPlaceRecording)
        journey_.capture_receipt_names();
    if (intent == fl_product_send::kSendIntentTakeFinished)
        journey_.capture_receipt_names();
    begin_failed_ = false;
    auto result = stage_.apply(intent, journey_, &hooks_, signedIn ? 1 : 0, now);
    if (begin_failed_) {
        begin_failed_ = false;
        result = stage_.apply(fl_product_send::kSendIntentCaptureFailed, journey_, &hooks_, 1, now);
    }
    return result;
}

void DeliveryService::advance(int64_t now)
{
    now_ = now;
    capture_.advance(now);
    const int notice = capture_.consumeNotice();
    if (notice == fl_product_record::kSessionNoticeWipeCompleted)
        (void)apply(fl_product_send::kSendIntentWipeComplete, now, true);
    else if (notice == fl_product_record::kSessionNoticeTakeFinished)
        (void)apply(fl_product_send::kSendIntentTakeFinished, now, true);
    else if (notice == fl_product_record::kSessionNoticeCaptureFailed)
        (void)apply(fl_product_send::kSendIntentCaptureFailed, now, true);
    (void)stage_.advance(now, journey_);
    pumpHttp(now);
    adoptFinished(now);
    const auto phase = journey_.ui_phase();
    if ((phase == fl_product_send::kSendUiPreparing || phase == fl_product_send::kSendUiSending) && now >= dot_due_) {
        dot_phase_ = (dot_phase_ + 1) % 3;
        dot_due_ = now + 400;
    }
}

bool DeliveryService::showsSketch() const
{
    const int place = stage_.place();
    return place == fl_product_send::kSendPlaceWipe || place == fl_product_send::kSendPlaceRecording ||
           place == fl_product_send::kSendPlaceReverse || place == fl_product_send::kSendPlaceFinishReverse;
}

bool DeliveryService::showsFeedback() const
{
    return stage_.place() == fl_product_send::kSendPlaceReceipt;
}

bool DeliveryService::couldntRecord() const
{
    return stage_.place() == fl_product_send::kSendPlaceCouldntRecord;
}

bool DeliveryService::sketchBoot() const
{
    const int place = stage_.place();
    return place == fl_product_send::kSendPlaceWipe || place == fl_product_send::kSendPlaceRecording ||
           place == fl_product_send::kSendPlaceReverse;
}

bool DeliveryService::sketchRed() const
{
    return stage_.place() == fl_product_send::kSendPlaceRecording;
}

bool DeliveryService::feedbackBoot() const
{
    if (stage_.place() != fl_product_send::kSendPlaceReceipt)
        return false;
    const auto phase = journey_.ui_phase();
    return phase == fl_product_send::kSendUiSending || phase == fl_product_send::kSendUiQueuedWait;
}

void DeliveryService::header(char *title, int titleCap, char *subtitle, int subtitleCap) const
{
    journey_.receipt_title(title, titleCap);
    journey_.receipt_subtitle(subtitle, subtitleCap);
}

void DeliveryService::body(char *out, int cap) const
{
    if (!out || cap <= 0)
        return;
    if (couldntRecord()) {
        copyText(out, cap, "couldn't record");
        return;
    }
    const char *label = journey_.body_label();
    int used = std::snprintf(out, static_cast<size_t>(cap), "%s", label ? label : "");
    const auto phase = journey_.ui_phase();
    if (used < 0 || used >= cap)
        return;
    if (phase != fl_product_send::kSendUiPreparing && phase != fl_product_send::kSendUiSending)
        return;
    for (int i = 0; i < dot_phase_ + 1 && used + 1 < cap; ++i)
        out[used++] = '.';
    out[used] = 0;
}

const char *DeliveryService::rememberedSelection() const
{
    return journey_.remembered_inbox_selection();
}

void DeliveryService::stageCaptureIdentity()
{
    fl_product_record::SendIdentity identity{};
    copyText(identity.client_message_id, fl_product_record::kMaxClientMessageId, journey_.capture_client_message_id());
    copyText(identity.sender_user_id, fl_product_record::kIdentityUser, user_);
    identity.recipient_count =
        journey_.copy_frozen_names(identity.recipient_names, fl_product_record::kIdentityRecipients);
    if (identity.recipient_count > fl_product_record::kIdentityRecipients)
        identity.recipient_count = fl_product_record::kIdentityRecipients;
    for (int i = 0; i < identity.recipient_count; ++i)
        copyText(identity.recipient_ids[i], fl_product_record::kIdentityUser, journey_.frozen_recipient_id(i));
    identity.valid = identity.client_message_id[0] ? 1 : 0;
    capture_.stageIdentity(identity);
}

void DeliveryService::pumpHttp(int64_t now)
{
    if (http_started_ && transport_.poll) {
        if (const auto *done = transport_.poll()) {
            fl_product_send::HttpAction action{};
            fl_product_send::SendTransportResult transport{};
            transport.transport_ok = done->perform_ok;
            transport.http_status = done->http_status;
            transport.body = done->body;
            transport.body_len = done->body_len;
            (void)journey_.outbox().apply_http_result(done->client_message_id, done->seq, action, transport, now);
        }
    }
    releaseTerminal();
    if (!journey_.outbox().active() || journey_.outbox().http_inflight() || !bearer_[0])
        return;
    if (transport_.start && !http_started_)
        http_started_ = transport_.start();
    if (!http_started_)
        return;
    fl_product_send::HttpAction action{};
    if (!journey_.outbox().prepare_http_action(&action, now))
        return;
    uint32_t seq = 0;
    journey_.outbox().note_http_sent(&seq);
    if (!transport_.submit ||
        !transport_.submit(action.method, action.path, action.body, action.body_len, action.is_json,
                           journey_.outbox().client_message_id(), seq, bearer_))
        journey_.outbox().cancel_http_sent();
}

void DeliveryService::releaseTerminal()
{
    const auto terminal = journey_.outbox().terminal();
    const char *id = journey_.outbox().client_message_id();
    if (!id || !id[0])
        return;
    if (terminal != fl_product_send::kSendTerminalAccepted && terminal != fl_product_send::kSendTerminalDiscarded)
        return;
    if (std::strcmp(media_id_, id) == 0)
        return;
    capture_.clearUpload(id);
    copyText(media_id_, static_cast<int>(sizeof(media_id_)), id);
}

void DeliveryService::adoptFinished(int64_t now)
{
    if (!user_[0] || journey_.outbox().active())
        return;
    if (startPayload(now))
        return;
}

bool DeliveryService::startPayload(int64_t now)
{
    if (!user_[0] || journey_.outbox().active())
        return false;
    for (int i = 0; i < fl_product_record::kAttachedFinishedSlots; ++i) {
        const auto *slot = capture_.finishedSlot(i);
        if (!slot || slot->audio_bytes == 0)
            continue;
        fl_product_record::SendIdentity identity{};
        if (!capture_.identityFor(slot->client_message_id, &identity) || !identity.valid)
            continue;
        if (std::strcmp(identity.sender_user_id, user_) != 0)
            continue;
        if (std::strcmp(slot->client_message_id, media_id_) == 0)
            continue;
        fl_product_send::FrozenPayload payload{};
        copyText(payload.client_message_id, fl_product_send::kMaxClientMessageId, slot->client_message_id);
        copyText(payload.sender_user_id, fl_product_send::kMaxUserId, identity.sender_user_id);
        payload.recipient_count = identity.recipient_count;
        if (payload.recipient_count > fl_product_send::kMaxRecipients)
            payload.recipient_count = fl_product_send::kMaxRecipients;
        for (int r = 0; r < payload.recipient_count; ++r)
            copyText(payload.recipient_user_ids[r], fl_product_send::kMaxUserId, identity.recipient_ids[r]);
        payload.audio_byte_count = slot->audio_bytes;
        payload.sketch_byte_count = slot->sketch_bytes;
        payload.sketch_present = slot->sketch_bytes > 0 ? 1 : 0;
        payload.duration_ms = slot->audio_bytes / 32u;
        if (payload.duration_ms < 1)
            payload.duration_ms = 1;
        char names[fl_product_send::kMaxRecipients][fl_product_send::kMaxName]{};
        for (int r = 0; r < payload.recipient_count; ++r)
            copyText(names[r], fl_product_send::kMaxName, identity.recipient_names[r]);
        fl_product_send::MediaPartReader reader{};
        reader.ctx = this;
        reader.read_at = readPart;
        journey_.set_signed_in_user(user_);
        journey_.install_receipt_names(names, payload.recipient_count);
        if (!journey_.bind_stored_payload(payload, reader))
            return false;
        if (!capture_.markUploading(payload.client_message_id))
            return false;
        journey_.outbox().begin_submit(now);
        return true;
    }
    return false;
}

int DeliveryService::readPart(void *context, int kind, uint32_t offset, uint8_t *dest, uint32_t length, uint32_t *read)
{
    auto *service = static_cast<DeliveryService *>(context);
    return service->capture_.readFinished(service->journey_.outbox().client_message_id(), kind, offset, dest, length,
                                          read)
               ? 1
               : 0;
}

void DeliveryService::pausePlayback(void *context, int64_t)
{
    auto *service = static_cast<DeliveryService *>(context);
    if (service->pause_)
        service->pause_(service->playback_);
}

const char *DeliveryService::selectedMessage(void *context)
{
    auto *service = static_cast<DeliveryService *>(context);
    return service->selected_ ? service->selected_(service->playback_) : "";
}

int DeliveryService::canStartSecond(void *context)
{
    return static_cast<DeliveryService *>(context)->capture_.canStartSecond() ? 1 : 0;
}

void DeliveryService::discardCapture(void *context)
{
    static_cast<DeliveryService *>(context)->capture_.discard();
}

void DeliveryService::cancelCapture(void *context)
{
    static_cast<DeliveryService *>(context)->capture_.cancel();
}

int DeliveryService::finishTake(void *context)
{
    return static_cast<DeliveryService *>(context)->capture_.finish() ? 1 : 0;
}

int DeliveryService::captureCanStart(void *context)
{
    return static_cast<DeliveryService *>(context)->capture_.canStart() ? 1 : 0;
}

void DeliveryService::beginWipe(void *context, const char *clientMessageId, int64_t now)
{
    auto *service = static_cast<DeliveryService *>(context);
    service->stageCaptureIdentity();
    if (!service->capture_.begin(clientMessageId, now))
        service->begin_failed_ = true;
}

void DeliveryService::beginReverse(void *context, int64_t now)
{
    static_cast<DeliveryService *>(context)->capture_.beginReverse(now);
}

void DeliveryService::beginCollapse(void *context, int64_t now)
{
    static_cast<DeliveryService *>(context)->capture_.beginCollapse(now);
}

} // namespace product_ui
