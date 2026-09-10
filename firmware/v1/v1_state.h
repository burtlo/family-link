#ifndef V1_STATE_H
#define V1_STATE_H

#include "v1_types.h"

typedef enum {
    V1_EV_NONE = 0,
    V1_EV_AUTH_OK,
    V1_EV_AUTH_FAIL,
    V1_EV_AUTH_STALE,
    V1_EV_ENTER_CONNECTING,
    V1_EV_ROSTER_READY,
    V1_EV_GOTO_PIN,
    V1_EV_GOTO_ROSTER,
    V1_EV_GOTO,
} v1_event_t;

void v1_state_init(void);
state_t v1_state_get(void);
void v1_state_apply(state_t st);
void v1_state_post(v1_event_t ev, uint32_t gen);
void v1_state_post_pick(v1_event_t ev, const char *pick_id);
void v1_state_post_goto(state_t st);
void v1_state_drain(void);
bool v1_state_may_probe(void);

#endif /* V1_STATE_H */
