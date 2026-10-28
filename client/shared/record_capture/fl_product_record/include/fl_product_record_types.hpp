#ifndef FL_PRODUCT_RECORD_TYPES_HPP
#define FL_PRODUCT_RECORD_TYPES_HPP

#include <stddef.h>
#include <stdint.h>

namespace fl_product_record
{

enum {
    kLogicalWidth = 120,
    kLogicalHeight = 120,
    kDisplayCanvas = 240,
    kDisplayRailWidth = 80,
    kSwatchCount = 6,
    kMaxSketchPoints = 4096,
    kDurationCapMs = 180000,
    kDurationCapUs = 180000000ULL,
    kAudioByteBudget = 5760000,
    kMaxMessageId = 40,
    kFlsk2PointBytes = 8,
    kMaxFlsk2Bytes = kMaxSketchPoints * kFlsk2PointBytes,
    kAttachedFinishedSlots = 2,
    kMaxClientMessageId = 48
};

enum FinishReason {
    kFinishNone = 0,
    kFinishRedStop = 1,
    kFinishDurationCap = 2,
    kFinishMicFailure = 3,
    kFinishCancelled = 4
};

enum MicState {
    kMicReady = 0,
    kMicUnavailableAtStart = 1,
    kMicFailedDuringCapture = 2
};

} /* namespace fl_product_record */

#endif
