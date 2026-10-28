#ifndef LAB06_CANCEL_SCHEDULE_H
#define LAB06_CANCEL_SCHEDULE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LAB06_ANIMATION_ADVANCE = 0,
    LAB06_ANIMATION_FINISH,
    LAB06_ANIMATION_CANCEL
} lab06_animation_action_t;

typedef struct {
    bool armed;
    bool transition_scheduled;
} lab06_cancel_control_t;

/* Manual input is accepted only while the stable panels are idle. Scripted
 * harness transitions keep their existing independent cancellation probe. */
static inline bool lab06_cancel_arm(lab06_cancel_control_t *control,
                                    bool scripted, bool transitioning)
{
    if (control == NULL || scripted || transitioning)
        return false;
    control->armed = true;
    return true;
}

/* Consume an idle arm exactly once when a transition has been accepted. */
static inline bool lab06_cancel_transition_started(
    lab06_cancel_control_t *control, bool scripted)
{
    if (control == NULL || scripted)
        return false;
    control->transition_scheduled = control->armed;
    control->armed = false;
    return control->transition_scheduled;
}

/* A failed attempt never carries an arm into a later transition. Scripted
 * cancellation has a separate harness schedule and does not use this control. */
static inline bool lab06_cancel_control_clear(
    lab06_cancel_control_t *control, bool scripted)
{
    if (control == NULL || scripted)
        return false;
    const bool was_pending = control->armed || control->transition_scheduled;
    control->armed = false;
    control->transition_scheduled = false;
    return was_pending;
}

static inline void lab06_cancel_transition_finished(
    lab06_cancel_control_t *control)
{
    if (control == NULL)
        return;
    control->armed = false;
    control->transition_scheduled = false;
}

/* Choose cancellation before normal completion so a delayed UI-timer callback
 * cannot turn the scripted midpoint probe into an uncancelled transition. */
static inline lab06_animation_action_t lab06_animation_action(
    bool cancel_probe_pending, int64_t elapsed_us,
    int64_t cancel_at_us, int64_t duration_us)
{
    if (cancel_probe_pending && elapsed_us >= cancel_at_us)
        return LAB06_ANIMATION_CANCEL;
    if (elapsed_us >= duration_us)
        return LAB06_ANIMATION_FINISH;
    return LAB06_ANIMATION_ADVANCE;
}

#endif
