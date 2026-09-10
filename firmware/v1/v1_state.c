#include "v1_state.h"

#include "v1_auth.h"
#include "v1_ui_common.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define V1_EV_Q_LEN 8

typedef struct {
    v1_event_t ev;
    uint32_t gen;
    state_t goto_st;
    char pick_id[16];
} v1_ev_msg_t;

static state_t s_st = ST_CONNECTING;
static QueueHandle_t s_ev_q;

void v1_state_init(void)
{
    s_st = ST_CONNECTING;
    if (!s_ev_q) {
        s_ev_q = xQueueCreate(V1_EV_Q_LEN, sizeof(v1_ev_msg_t));
    }
}

state_t v1_state_get(void)
{
    return s_st;
}

void v1_state_apply(state_t st)
{
    s_st = st;
}

void v1_state_post(v1_event_t ev, uint32_t gen)
{
    if (!s_ev_q || ev == V1_EV_NONE) {
        return;
    }
    v1_ev_msg_t msg = { .ev = ev, .gen = gen, .pick_id = {0} };
    (void)xQueueSend(s_ev_q, &msg, 0);
}

void v1_state_post_pick(v1_event_t ev, const char *pick_id)
{
    if (!s_ev_q || ev == V1_EV_NONE) {
        return;
    }
    v1_ev_msg_t msg = { .ev = ev, .gen = 0, .goto_st = ST_CONNECTING, .pick_id = {0} };
    if (pick_id) {
        strncpy(msg.pick_id, pick_id, sizeof(msg.pick_id) - 1);
    }
    (void)xQueueSend(s_ev_q, &msg, 0);
}

void v1_state_post_goto(state_t st)
{
    if (!s_ev_q) {
        return;
    }
    v1_ev_msg_t msg = { .ev = V1_EV_GOTO, .gen = 0, .goto_st = st, .pick_id = {0} };
    (void)xQueueSend(s_ev_q, &msg, 0);
}

void v1_state_drain(void)
{
    if (!s_ev_q) {
        return;
    }
    v1_ev_msg_t msg;
    bool changed = false;
    while (xQueueReceive(s_ev_q, &msg, 0) == pdTRUE) {
        state_t before = s_st;
        switch (msg.ev) {
        case V1_EV_AUTH_OK:
            if (s_st == ST_PIN && msg.gen == v1_auth_generation()) {
                v1_auth_on_login_ok();
                s_st = ST_CAROUSEL;
            }
            break;
        case V1_EV_AUTH_FAIL:
            break;
        case V1_EV_AUTH_STALE:
            break;
        case V1_EV_ENTER_CONNECTING:
            if (!v1_auth_login_active()) {
                s_st = ST_CONNECTING;
            }
            break;
        case V1_EV_ROSTER_READY:
            if (s_st == ST_CONNECTING && !v1_auth_login_active()) {
                s_st = ST_ROSTER;
            }
            break;
        case V1_EV_GOTO_PIN:
            if (msg.pick_id[0]) {
                v1_auth_set_pick_id(msg.pick_id);
            }
            s_st = ST_PIN;
            break;
        case V1_EV_GOTO_ROSTER:
            s_st = ST_ROSTER;
            break;
        case V1_EV_GOTO:
            s_st = msg.goto_st;
            break;
        default:
            break;
        }
        if (s_st != before) {
            changed = true;
        }
    }
    if (changed) {
        v1_ui_request_repaint();
    }
}

bool v1_state_may_probe(void)
{
    return !v1_auth_login_active();
}
