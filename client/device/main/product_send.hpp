#pragma once

#include "lvgl.h"

#include <cstdint>

namespace product_send
{

enum class Intent {
    Red = 1,
    Boot = 2,
    Mute = 3,
    WipeComplete = 4,
    CaptureFailed = 5,
    TakeFinished = 6,
};

struct StageResult {
    int place;
    int shell_stage;
    int changed;
    int ignored;
    int opened_volume;
    int show_record_canvas;
    int show_split_pane;
    int left_wait;
};

bool attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot);
void detach();
void release_widgets();
void forget_widgets();
bool build_widgets(lv_obj_t *upper_slot, lv_obj_t *lower_slot);
void service(int64_t now_ms);
/* Called only by product_stage::present(). */
void present(bool visible);
void note_inbox();
void set_wifi(bool up);
void set_refresh_failed(bool failed);
void on_signed_in(const char *user_id);
StageResult apply_intent(Intent intent, bool signed_in);

} /* namespace product_send */
