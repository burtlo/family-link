#include "lab02/snap.h"

#include <assert.h>
#include <stdbool.h>

extern void lab02_lvgl_host_finish_active_anim(void);

typedef struct {
    int session_lock_depth;
} snap_done_ctx_t;

static void on_snap_done(void *user)
{
    snap_done_ctx_t *ctx = (snap_done_ctx_t *)user;
    if (!ctx) {
        return;
    }
    if (ctx->session_lock_depth > 0) {
        ctx->session_lock_depth--;
    }
}

int main(void)
{
    lv_obj_t scroller = {.scroll_x = 0};
    lab02_snap_state_t snap = {.item_count = 16, .scroll_lock = false};
    snap_done_ctx_t ctx = {.session_lock_depth = 0};

    ctx.session_lock_depth++;
    lab02_snap_animate_to(&scroller, &snap, 100, on_snap_done, &ctx);
    assert(snap.scroll_lock);

    ctx.session_lock_depth++;
    lab02_snap_animate_to(&scroller, &snap, 200, on_snap_done, &ctx);
    assert(snap.scroll_lock);
    assert(ctx.session_lock_depth == 1);

    lab02_lvgl_host_finish_active_anim();
    assert(!snap.scroll_lock);
    assert(ctx.session_lock_depth == 0);
    assert(scroller.scroll_x == 200);
    return 0;
}
