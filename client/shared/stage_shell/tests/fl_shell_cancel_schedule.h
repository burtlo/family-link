#ifndef FL_SHELL_CANCEL_SCHEDULE_H
#define FL_SHELL_CANCEL_SCHEDULE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FL_SHELL_ANIMATION_ADVANCE = 0,
    FL_SHELL_ANIMATION_FINISH,
    FL_SHELL_ANIMATION_CANCEL
} fl_shell_animation_action_t;

typedef struct {
    bool armed;
    bool transition_scheduled;
} fl_shell_cancel_control_t;

/* Manual input is accepted only while the stable panels are idle. Scripted
 * harness transitions keep their existing independent cancellation probe. */
static inline bool fl_shell_cancel_arm(fl_shell_cancel_control_t *control,
                                       bool scripted, bool transitioning)
{
    if (control == NULL || scripted || transitioning)
        return false;
    control->armed = true;
    return true;
}

/* Consume an idle arm exactly once when a transition has been accepted. */
static inline bool fl_shell_cancel_transition_started(
    fl_shell_cancel_control_t *control, bool scripted)
{
    if (control == NULL || scripted)
        return false;
    control->transition_scheduled = control->armed;
    control->armed = false;
    return control->transition_scheduled;
}

/* A failed attempt never carries an arm into a later transition. Scripted
 * cancellation has a separate harness schedule and does not use this control. */
static inline bool fl_shell_cancel_control_clear(
    fl_shell_cancel_control_t *control, bool scripted)
{
    if (control == NULL || scripted)
        return false;
    const bool was_pending = control->armed || control->transition_scheduled;
    control->armed = false;
    control->transition_scheduled = false;
    return was_pending;
}

static inline void fl_shell_cancel_transition_finished(
    fl_shell_cancel_control_t *control)
{
    if (control == NULL)
        return;
    control->armed = false;
    control->transition_scheduled = false;
}

/* Choose cancellation before normal completion so a delayed UI-timer callback
 * cannot turn the scripted midpoint probe into an uncancelled transition. */
static inline fl_shell_animation_action_t fl_shell_animation_action(
    bool cancel_probe_pending, int64_t elapsed_us,
    int64_t cancel_at_us, int64_t duration_us)
{
    if (cancel_probe_pending && elapsed_us >= cancel_at_us)
        return FL_SHELL_ANIMATION_CANCEL;
    if (elapsed_us >= duration_us)
        return FL_SHELL_ANIMATION_FINISH;
    return FL_SHELL_ANIMATION_ADVANCE;
}

#endif
