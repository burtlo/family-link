#pragma once

#include <stdbool.h>

void trace_host_set_task_create_failure(bool fail);
unsigned trace_host_task_create_count(void);
unsigned trace_host_last_task_stack_words(void);
void trace_host_set_sink_blocked(bool blocked);
void trace_host_reset_sink_observation(void);
void trace_host_wait_sink_entered(void);
