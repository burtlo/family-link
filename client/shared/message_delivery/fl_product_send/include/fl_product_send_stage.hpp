#ifndef FL_PRODUCT_SEND_STAGE_HPP
#define FL_PRODUCT_SEND_STAGE_HPP

#include "fl_product_send_journey.hpp"

#include <stdint.h>

namespace fl_product_send {

/* Places this reducer owns. Ring-wipe is a substate of the record shell
 * stage. Preparing, sending, sent, and the connectivity wait share receipt. */
enum SendPlace {
    kSendPlaceInbox = 0,
    kSendPlacePicker = 1,
    kSendPlaceWipe = 2,
    kSendPlaceRecording = 3,
    kSendPlaceReceipt = 4,
    /* Ring collapse back to the picker after a wipe that cannot record. */
    kSendPlaceReverse = 5,
    /* Picker shell, lower slot holds "couldn't record" until the dwell ends. */
    kSendPlaceCouldntRecord = 6,
    /* Ring collapse after a finished take. Upload starts when it ends. */
    kSendPlaceFinishReverse = 7
};

enum SendIntent {
    kSendIntentNone = 0,
    kSendIntentRed = 1,
    kSendIntentBoot = 2,
    kSendIntentMute = 3,
    kSendIntentWipeComplete = 4,
    kSendIntentCaptureFailed = 5,
    kSendIntentTakeFinished = 6
};

enum {
    /* Matches LAB10_RING_DURATION_MS. The record session uses that macro. */
    kSendRingReverseMs = 600,
    kSendCouldntRecordHoldMs = 2000
};

/* Playback and capture stay behind hooks so this owner can run on the host.
 * The device fills them with CarouselSession::pause_for_record and the
 * record session storage calls. Null hooks skip that side effect. */
struct SendStageHooks {
    void *playback;
    void (*pause_for_record)(void *playback, int64_t now_ms);
    const char *(*selected_message_id)(void *playback);
    void *capture;
    int (*can_start_second_recording)(void *capture);
    void (*discard_in_progress)(void *capture);
    void (*cancel_recording)(void *capture);
    int (*finish_take)(void *capture);
    /* Null means the microphone can start. Zero refuses before the wipe. */
    int (*capture_can_start)(void *capture);
    void (*begin_wipe)(void *capture, const char *client_message_id, int64_t now_ms);
    void (*begin_reverse)(void *capture, int64_t now_ms);
    /* Visual collapse after a committed take. Does not discard storage. */
    void (*begin_finish_collapse)(void *capture, int64_t now_ms);
};

struct SendStageResult {
    int place;
    int changed;
    int ignored;
    int paused;
    int remembered;
    int opened_volume;
    int discarded;
    int finished;
    int left_wait;
    int show_record_canvas;
    int show_split_pane;
    int reversed;
    int show_couldnt_record;
    int upload_ready;
    int returned_inbox;
};

/* Single writer for picker, wipe, record, and receipt. Callers emit intents. */
class SendStageReducer {
public:
    SendStageReducer();

    void reset();
    void note_inbox();
    int place() const;
    const char *lower_status() const;
    SendStageResult apply(SendIntent intent, SendJourney &journey, const SendStageHooks *hooks,
                          int signed_in, int64_t now_ms);
    /* Ends the reverse collapse and the couldn't-record dwell. */
    SendStageResult advance(int64_t now_ms);
    /* Also elapses the sent dwell. leave_wait does not move the current place. */
    SendStageResult advance(int64_t now_ms, SendJourney &journey);

private:
    void present(SendStageResult *result) const;
    void enter(int place, SendStageResult *result);
    void pause_and_remember(SendJourney &journey, const SendStageHooks *hooks, int64_t now_ms,
                            SendStageResult *result);
    void discard_capture(const SendStageHooks *hooks, SendStageResult *result);
    void arm_couldnt_record(int64_t now_ms, SendStageResult *result);
    void arm_finish_collapse(int64_t now_ms, const SendStageHooks *hooks, SendStageResult *result);

    int place_;
    int64_t hold_until_ms_;
};

} /* namespace fl_product_send */

#endif
