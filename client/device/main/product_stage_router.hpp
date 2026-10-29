#pragma once

/* The one user-visible surface owner per tick. See
 * docs/standards/device-presentation-invariants.md. */

namespace product_stage
{

enum class Screen {
    Login = 0,
    Inbox,
    Volume,
    Send,
    Record,
    Count,
};

[[nodiscard]] Screen screen();
void present();
void on_signed_in();
int on_red_record();
int on_boot();
void on_mute();
int on_wipe_complete();
int on_capture_failed();
int on_take_finished();

} /* namespace product_stage */
