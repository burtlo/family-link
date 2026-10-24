#include "fl_product_send_stage.hpp"

#include <string.h>

namespace fl_product_send {
namespace {

int finished_upload_active(const SendJourney &journey)
{
    SendUiPhase ui = journey.ui_phase();
    if (ui == kSendUiPreparing || ui == kSendUiSending || ui == kSendUiQueuedWait) {
        return 1;
    }
    return 0;
}

int body_is_connectivity_wait(const char *body)
{
    if (body == 0) {
        return 0;
    }
    if (strcmp(body, "no Wi-Fi") == 0 || strcmp(body, "connecting") == 0) {
        return 1;
    }
    return 0;
}

/* Preparing keeps Boot. The sending body and the queued wait are the same
 * finished upload: Boot leaves and does not cancel it. Sent keeps Boot. */
int receipt_boot_leaves(const SendJourney &journey)
{
    SendUiPhase ui = journey.ui_phase();
    if (ui == kSendUiSent || ui == kSendUiCouldntSend || ui == kSendUiPreparing) {
        return 0;
    }
    if (ui == kSendUiQueuedWait || ui == kSendUiSending) {
        return 1;
    }
    return body_is_connectivity_wait(journey.body_label());
}

int can_start_second(const SendStageHooks *hooks)
{
    if (hooks == 0 || hooks->can_start_second_recording == 0) {
        return 0;
    }
    return hooks->can_start_second_recording(hooks->capture) ? 1 : 0;
}

} /* namespace */

SendStageReducer::SendStageReducer() : place_(kSendPlaceInbox), hold_until_ms_(0) {}

void SendStageReducer::reset()
{
    place_ = kSendPlaceInbox;
    hold_until_ms_ = 0;
}

void SendStageReducer::note_inbox() { place_ = kSendPlaceInbox; }

int SendStageReducer::place() const { return place_; }

const char *SendStageReducer::lower_status() const
{
    if (place_ == kSendPlaceCouldntRecord) {
        return "couldn't record";
    }
    return "";
}

void SendStageReducer::present(SendStageResult *result) const
{
    result->place = place_;
    result->show_couldnt_record = place_ == kSendPlaceCouldntRecord ? 1 : 0;
    if (place_ == kSendPlaceWipe || place_ == kSendPlaceRecording || place_ == kSendPlaceReverse ||
        place_ == kSendPlaceFinishReverse) {
        result->show_record_canvas = 1;
        result->show_split_pane = 0;
        return;
    }
    result->show_record_canvas = 0;
    result->show_split_pane = 1;
}

void SendStageReducer::enter(int place, SendStageResult *result)
{
    place_ = place;
    result->changed = 1;
    result->ignored = 0;
    present(result);
}

void SendStageReducer::pause_and_remember(SendJourney &journey, const SendStageHooks *hooks,
                                          int64_t now_ms, SendStageResult *result)
{
    const char *message_id = 0;
    if (hooks != 0 && hooks->pause_for_record != 0) {
        hooks->pause_for_record(hooks->playback, now_ms);
        result->paused = 1;
    }
    if (hooks != 0 && hooks->selected_message_id != 0) {
        message_id = hooks->selected_message_id(hooks->playback);
    }
    journey.remember_inbox_selection(message_id);
    result->remembered = 1;
}

void SendStageReducer::discard_capture(const SendStageHooks *hooks, SendStageResult *result)
{
    if (hooks == 0) {
        return;
    }
    if (hooks->cancel_recording != 0) {
        hooks->cancel_recording(hooks->capture);
    }
    if (hooks->discard_in_progress != 0) {
        hooks->discard_in_progress(hooks->capture);
    }
    result->discarded = 1;
}

void SendStageReducer::arm_couldnt_record(int64_t now_ms, SendStageResult *result)
{
    hold_until_ms_ = now_ms + kSendCouldntRecordHoldMs;
    enter(kSendPlaceCouldntRecord, result);
}

void SendStageReducer::arm_finish_collapse(int64_t now_ms, const SendStageHooks *hooks,
                                           SendStageResult *result)
{
    result->finished = 1;
    result->reversed = 1;
    result->upload_ready = 0;
    hold_until_ms_ = now_ms + kSendRingReverseMs;
    enter(kSendPlaceFinishReverse, result);
    if (hooks != 0 && hooks->begin_finish_collapse != 0) {
        hooks->begin_finish_collapse(hooks->capture, now_ms);
    }
}

SendStageResult SendStageReducer::advance(int64_t now_ms)
{
    SendStageResult result;
    memset(&result, 0, sizeof(result));
    present(&result);
    if (place_ == kSendPlaceReverse && now_ms >= hold_until_ms_) {
        arm_couldnt_record(now_ms, &result);
        return result;
    }
    if (place_ == kSendPlaceCouldntRecord && now_ms >= hold_until_ms_) {
        enter(kSendPlacePicker, &result);
        return result;
    }
    if (place_ == kSendPlaceFinishReverse && now_ms >= hold_until_ms_) {
        enter(kSendPlaceReceipt, &result);
        result.upload_ready = 1;
        return result;
    }
    return result;
}

SendStageResult SendStageReducer::advance(int64_t now_ms, SendJourney &journey)
{
    SendUiPhase before = journey.ui_phase();
    int left = journey.outbox().left_wait();
    SendStageResult result = advance(now_ms);
    journey.advance_to(now_ms);
    /* A collapse that just opened the receipt must not also leave it.
     * leave_wait skips the sent dwell and does not pull the inbox or a
     * new recording back onto this screen. */
    if (result.upload_ready != 0 || place_ != kSendPlaceReceipt) {
        return result;
    }
    if (journey.ui_phase() != kSendUiHidden) {
        return result;
    }
    if (journey.outbox().terminal() == kSendTerminalAccepted && (left || before == kSendUiSent)) {
        enter(kSendPlaceInbox, &result);
        result.returned_inbox = 1;
        return result;
    }
    /* couldn't send dwell ended. Inbox, and the attempt stays discarded. */
    if (journey.outbox().terminal() == kSendTerminalDiscarded && before == kSendUiCouldntSend) {
        enter(kSendPlaceInbox, &result);
        result.returned_inbox = 1;
    }
    return result;
}

SendStageResult SendStageReducer::apply(SendIntent intent, SendJourney &journey,
                                        const SendStageHooks *hooks, int signed_in, int64_t now_ms)
{
    SendStageResult result;
    memset(&result, 0, sizeof(result));
    present(&result);

    if (intent == kSendIntentWipeComplete) {
        if (place_ != kSendPlaceWipe) {
            result.ignored = 1;
            return result;
        }
        enter(kSendPlaceRecording, &result);
        return result;
    }

    if (intent == kSendIntentCaptureFailed) {
        if (place_ != kSendPlaceWipe) {
            result.ignored = 1;
            return result;
        }
        discard_capture(hooks, &result);
        journey.clear_capture_freeze();
        result.reversed = 1;
        hold_until_ms_ = now_ms + kSendRingReverseMs;
        enter(kSendPlaceReverse, &result);
        if (hooks != 0 && hooks->begin_reverse != 0) {
            hooks->begin_reverse(hooks->capture, now_ms);
        }
        return result;
    }

    if (intent == kSendIntentTakeFinished) {
        if (place_ != kSendPlaceRecording) {
            result.ignored = 1;
            return result;
        }
        /* The record session already committed this take. Collapse before upload. */
        arm_finish_collapse(now_ms, hooks, &result);
        return result;
    }

    if (intent == kSendIntentMute) {
        if (place_ == kSendPlacePicker || place_ == kSendPlaceWipe || place_ == kSendPlaceRecording ||
            place_ == kSendPlaceReverse || place_ == kSendPlaceCouldntRecord) {
            discard_capture(hooks, &result);
            journey.clear_capture_freeze();
            if (finished_upload_active(journey)) {
                journey.picker().reset();
                journey.on_mute_after_finish(1);
            } else {
                journey.on_mute_during_capture();
            }
            enter(kSendPlaceInbox, &result);
            return result;
        }
        /* A finished take stays stored. Nothing uploads while signed out. */
        if (place_ == kSendPlaceReceipt || place_ == kSendPlaceFinishReverse || finished_upload_active(journey) ||
            journey.outbox().active()) {
            journey.on_mute_after_finish(1);
            enter(kSendPlaceInbox, &result);
            return result;
        }
        result.ignored = 1;
        return result;
    }

    if (!signed_in) {
        result.ignored = 1;
        return result;
    }

    if (place_ == kSendPlaceInbox && intent == kSendIntentBoot) {
        result.opened_volume = 1;
        result.changed = 1;
        present(&result);
        return result;
    }

    if (place_ == kSendPlaceInbox && intent == kSendIntentRed) {
        SendUiPhase ui = journey.ui_phase();
        /* SCN-028 / EXP-031: preparing and the connectivity wait are still
         * this send. Red pauses playback, remembers the inbox card, and
         * returns to the receipt. It does not open recipient selection,
         * start a take, or cancel the outbox. */
        if (ui == kSendUiPreparing || ui == kSendUiQueuedWait) {
            if (journey.outbox().signed_in_matches_sender()) {
                pause_and_remember(journey, hooks, now_ms, &result);
                enter(kSendPlaceReceipt, &result);
                return result;
            }
            /* Another person's pending send is not this wait and not a new take
             * unless storage can hold one. */
            if (!can_start_second(hooks)) {
                result.ignored = 1;
                return result;
            }
        }
        /* SCN-057 / EXP-046: a streaming send stays in flight. Red pauses
         * playback, remembers the inbox card, and opens the picker. The new
         * take starts only after a recipient is chosen. If storage cannot
         * hold that take, stay on the inbox and do not cancel the stream. */
        if (ui == kSendUiSending) {
            if (!can_start_second(hooks)) {
                result.ignored = 1;
                return result;
            }
            pause_and_remember(journey, hooks, now_ms, &result);
            enter(kSendPlacePicker, &result);
            return result;
        }
        pause_and_remember(journey, hooks, now_ms, &result);
        enter(kSendPlacePicker, &result);
        return result;
    }

    if (place_ == kSendPlacePicker && intent == kSendIntentRed) {
        if (journey.picker().selected_count() <= 0) {
            result.ignored = 1;
            return result;
        }
        if (hooks != 0 && hooks->capture_can_start != 0 && hooks->capture_can_start(hooks->capture) == 0) {
            journey.clear_capture_freeze();
            arm_couldnt_record(now_ms, &result);
            return result;
        }
        journey.freeze_capture_recipients();
        if (journey.frozen_recipient_count() <= 0) {
            journey.clear_capture_freeze();
            result.ignored = 1;
            return result;
        }
        enter(kSendPlaceWipe, &result);
        if (hooks != 0 && hooks->begin_wipe != 0) {
            hooks->begin_wipe(hooks->capture, journey.capture_client_message_id(), now_ms);
        }
        return result;
    }

    if (place_ == kSendPlacePicker && intent == kSendIntentBoot) {
        journey.picker().reset();
        journey.clear_capture_freeze();
        enter(kSendPlaceInbox, &result);
        return result;
    }

    if (place_ == kSendPlaceCouldntRecord && intent == kSendIntentRed) {
        result.ignored = 1;
        return result;
    }

    if (place_ == kSendPlaceCouldntRecord && intent == kSendIntentBoot) {
        journey.picker().reset();
        journey.clear_capture_freeze();
        enter(kSendPlaceInbox, &result);
        return result;
    }

    if (place_ == kSendPlaceReverse && intent == kSendIntentRed) {
        result.ignored = 1;
        return result;
    }

    if (place_ == kSendPlaceReverse && intent == kSendIntentBoot) {
        discard_capture(hooks, &result);
        journey.picker().reset();
        journey.clear_capture_freeze();
        enter(kSendPlaceInbox, &result);
        return result;
    }

    if (place_ == kSendPlaceWipe && intent == kSendIntentRed) {
        result.ignored = 1;
        return result;
    }

    if ((place_ == kSendPlaceWipe || place_ == kSendPlaceRecording) && intent == kSendIntentBoot) {
        discard_capture(hooks, &result);
        journey.picker().reset();
        journey.clear_capture_freeze();
        enter(kSendPlaceInbox, &result);
        return result;
    }

    if (place_ == kSendPlaceFinishReverse && (intent == kSendIntentRed || intent == kSendIntentBoot)) {
        result.ignored = 1;
        return result;
    }

    if (place_ == kSendPlaceRecording && intent == kSendIntentRed) {
        if (hooks != 0 && hooks->finish_take != 0 && hooks->finish_take(hooks->capture) == 0) {
            result.ignored = 1;
            return result;
        }
        arm_finish_collapse(now_ms, hooks, &result);
        return result;
    }

    if (place_ == kSendPlaceReceipt && intent == kSendIntentRed) {
        result.ignored = 1;
        return result;
    }

    if (place_ == kSendPlaceReceipt && intent == kSendIntentBoot) {
        if (!receipt_boot_leaves(journey)) {
            result.ignored = 1;
            return result;
        }
        journey.leave_wait();
        result.left_wait = 1;
        enter(kSendPlaceInbox, &result);
        return result;
    }

    result.ignored = 1;
    return result;
}

} /* namespace fl_product_send */
