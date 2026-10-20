#include "lab05_peak_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed;

static void expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s\n", message);
        g_failed = 1;
    }
}

static int finish(void)
{
    return g_failed ? 1 : 0;
}

static int test_handles(void)
{
    lab05_peak_session session;
    lab05_peak_handle old_handle;
    const lab05_peak_operation *op;
    lab05_peak_image_slot *slot;
    uint8_t byte = 0;
    g_failed = 0;
    lab05_peak_session_init(&session);
    expect(lab05_peak_run_stale_callback(&session, 50, &old_handle) == 0, "stale run");
    slot = &session.images[old_handle.slot];
    expect(slot->pixels == NULL, "pixels freed");
    expect(slot->reads == 0, "no read of freed image");
    expect(slot->writes == 0, "no write of freed image");
    expect(lab05_peak_read_image(&session, old_handle, &byte) != 0, "read rejected");
    expect(lab05_peak_write_image(&session, old_handle, 0x11) != 0, "write rejected");
    expect(slot->reads == 0, "read counter stayed zero");
    expect(slot->writes == 0, "write counter stayed zero");
    op = lab05_peak_find_operation(&session, 50);
    expect(op != NULL, "stale op");
    if (op != NULL) {
        expect(op->terminal == LAB05_PEAK_TERMINAL_STALE, "handles terminal");
        expect(op->terminal_count == 1u, "handles terminal count");
    }
    if (!g_failed) {
        printf("HANDLES reads_after_delete=0 writes_through_old_handle=0 freed_pixels=1\n");
    }
    return finish();
}

static int test_stale(void)
{
    lab05_peak_session session;
    lab05_peak_handle old_handle;
    const lab05_peak_operation *op;
    g_failed = 0;
    lab05_peak_session_init(&session);
    expect(lab05_peak_run_stale_callback(&session, 50, &old_handle) == 0, "stale run");
    op = lab05_peak_find_operation(&session, 50);
    expect(op != NULL, "stale op");
    if (op != NULL) {
        expect(strcmp(lab05_peak_scenario_name(op->scenario), "P-stale-callback") == 0, "scenario");
        expect(op->terminal == LAB05_PEAK_TERMINAL_STALE, "terminal");
        expect(op->terminal_count == 1u, "count");
        expect(strcmp(op->reason, "stale") == 0, "reason");
    }
    expect(session.images[old_handle.slot].writes == 0, "writes");
    expect(session.images[old_handle.slot].reads == 0, "reads");
    expect(lab05_peak_set_terminal(&session, 50, LAB05_PEAK_SCENARIO_STALE_CALLBACK,
                                   LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_OP_REPETITION,
                                   LAB05_PEAK_TERMINAL_SUCCESS, "ok") != 0,
           "second terminal");
    op = lab05_peak_find_operation(&session, 50);
    expect(op != NULL && op->terminal == LAB05_PEAK_TERMINAL_STALE && op->terminal_count == 1u,
           "terminal unchanged");
    if (!g_failed) {
        printf("STALE scenario=P-stale-callback terminal=stale terminal_count=1 "
               "operation_id=50 writes_through_old_handle=0\n");
    }
    return finish();
}

static int test_alloc_fail(void)
{
    lab05_peak_session session;
    lab05_peak_handle old_handle;
    const lab05_peak_operation *op;
    uint8_t byte = 0;
    g_failed = 0;
    lab05_peak_session_init(&session);
    expect(session.live_images == 1u, "started with one image");
    expect(lab05_peak_run_alloc_fail(&session, 40, &old_handle) == 0, "alloc fail run");
    op = lab05_peak_find_operation(&session, 40);
    expect(op != NULL, "alloc op");
    if (op != NULL) {
        expect(op->terminal == LAB05_PEAK_TERMINAL_FAILURE, "failure");
        expect(strcmp(op->reason, "alloc") == 0, "alloc reason");
        expect(op->terminal_count == 1u, "one terminal");
        expect(op->control == LAB05_PEAK_CONTROL_PERSISTENT, "persistent control");
        expect(strcmp(lab05_peak_scenario_name(op->scenario), "P-alloc-fail") == 0, "scenario");
    }
    expect(session.images[old_handle.slot].pixels == NULL, "old pixels freed");
    expect(session.images[old_handle.slot].reads == 0, "production did not read");
    expect(session.images[old_handle.slot].writes == 0, "production did not write");
    expect(lab05_peak_read_image(&session, old_handle, &byte) != 0, "later read rejected");
    expect(session.images[old_handle.slot].reads == 0, "still no pixel read");
    expect(lab05_peak_set_terminal(&session, 40, LAB05_PEAK_SCENARIO_ALLOC_FAIL,
                                   LAB05_PEAK_CONTROL_REPLACE, LAB05_PEAK_OP_REPETITION,
                                   LAB05_PEAK_TERMINAL_SUCCESS, "ok") != 0,
           "second");
    op = lab05_peak_find_operation(&session, 40);
    expect(op != NULL && op->terminal_count == 1u && op->terminal == LAB05_PEAK_TERMINAL_FAILURE,
           "still failure");
    if (!g_failed) {
        printf("ALLOC_FAIL scenario=P-alloc-fail terminal=failure reason=alloc "
               "terminal_count=1 control=persistent reads_after_delete=0 second_terminal=rejected\n");
    }
    return finish();
}

static int test_cancel(void)
{
    lab05_peak_session session;
    const lab05_peak_operation *anim;
    const lab05_peak_operation *audio;
    const lab05_peak_operation *page;
    g_failed = 0;
    lab05_peak_session_init(&session);
    expect(session.live_images == LAB05_PEAK_STEADY_LIVE_IMAGES, "steady start");
    expect(lab05_peak_run_cancel(&session, 11, 12, 13) == 0, "cancel run");
    anim = lab05_peak_find_operation(&session, 11);
    audio = lab05_peak_find_operation(&session, 12);
    page = lab05_peak_find_operation(&session, 13);
    expect(anim != NULL && audio != NULL && page != NULL, "ops present");
    if (anim != NULL) {
        expect(anim->terminal == LAB05_PEAK_TERMINAL_CANCELLED, "cancelled");
        expect(anim->terminal_count == 1u, "anim count");
        expect(anim->live_image_count == LAB05_PEAK_STEADY_LIVE_IMAGES, "recorded live");
        expect(strcmp(lab05_peak_scenario_name(anim->scenario), "P-cancel") == 0, "scenario");
    }
    expect(session.live_images == LAB05_PEAK_STEADY_LIVE_IMAGES, "session live");
    expect(session.incoming_y == 120, "cancelled mid animation");
    expect(session.animation_elapsed_ms == 200u, "elapsed during cover");
    expect(session.stage == LAB05_PEAK_STAGE_STEADY, "session ended the transition");
    expect(session.audio_running == 0, "audio worker finished");
    expect(session.page_count == LAB05_PEAK_PAGE_RECORDS, "page worker finished");
    expect(session.running_operations == 0u, "workers and animation released");
    if (audio != NULL) {
        expect(audio->terminal == LAB05_PEAK_TERMINAL_SUCCESS, "audio terminal");
        expect(audio->terminal_count == 1u, "audio count");
        expect(audio->kind == LAB05_PEAK_OP_WORKER, "audio kind");
    }
    if (page != NULL) {
        expect(page->terminal == LAB05_PEAK_TERMINAL_SUCCESS, "page terminal");
        expect(page->terminal_count == 1u, "page count");
    }
    expect(lab05_peak_set_terminal(&session, 11, LAB05_PEAK_SCENARIO_CANCEL,
                                   LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_OP_REPETITION,
                                   LAB05_PEAK_TERMINAL_SUCCESS, "ok") != 0,
           "second cancel terminal");
    anim = lab05_peak_find_operation(&session, 11);
    expect(anim != NULL && anim->terminal == LAB05_PEAK_TERMINAL_CANCELLED && anim->terminal_count == 1u,
           "cancel terminal sticky");
    if (!g_failed) {
        printf("CANCEL scenario=P-cancel operation_id=11 terminal=cancelled terminal_count=1 "
               "live_image_count=%u steady_live_images=%u\n",
               session.live_images, LAB05_PEAK_STEADY_LIVE_IMAGES);
        printf("WORKERS audio_terminal=success audio_terminal_count=1 "
               "page_terminal=success page_terminal_count=1\n");
    }
    return finish();
}

static int fill_queue(lab05_peak_session *session, int audio, uint64_t first_id)
{
    unsigned i;
    for (i = 0; i < LAB05_PEAK_QUEUE_CAPACITY; i++) {
        int rc = audio ? lab05_peak_queue_push_audio(session, first_id + i)
                       : lab05_peak_queue_push_page(session, first_id + i);
        if (rc != 0) {
            return -1;
        }
    }
    return 0;
}

static int test_overflow(void)
{
    lab05_peak_session session;
    const lab05_peak_operation *first_reject;
    const lab05_peak_queue *audio;
    const lab05_peak_queue *page;
    unsigned i;
    g_failed = 0;
    lab05_peak_session_init(&session);
    expect(fill_queue(&session, 1, 101) == 0, "fill audio");
    expect(lab05_peak_queue_push_audio(&session, 109) != 0, "audio reject");
    expect(lab05_peak_queue_push_audio(&session, 110) != 0, "audio second reject");
    expect(fill_queue(&session, 0, 201) == 0, "fill page");
    expect(lab05_peak_queue_push_page(&session, 209) != 0, "page reject");
    audio = &session.audio_stop;
    page = &session.page_apply;
    expect(audio->capacity == 8u && page->capacity == 8u, "named capacity");
    expect(audio->count == 8u && page->count == 8u, "count preserved");
    expect(audio->overflow_count == 2u && page->overflow_count == 1u, "overflow counted");
    expect(audio->dropped_count == 0u && page->dropped_count == 0u, "nothing dropped");
    expect(audio->items[0].operation_id == 101 && audio->items[7].operation_id == 108, "audio ids");
    expect(page->items[0].operation_id == 201 && page->items[7].operation_id == 208, "page ids");
    first_reject = lab05_peak_find_operation(&session, 109);
    expect(first_reject != NULL && first_reject->terminal == LAB05_PEAK_TERMINAL_FAILURE, "overflow terminal");
    expect(first_reject != NULL && strcmp(first_reject->reason, "overflow") == 0, "overflow reason");
    expect(first_reject != NULL && first_reject->terminal_count == 1u, "one overflow terminal");
    expect(lab05_peak_find_operation(&session, 110) == NULL, "second reject has no terminal");
    expect(strcmp(audio->name, "audio-stop") == 0, "audio queue name");
    expect(strcmp(page->name, "page-apply") == 0, "page queue name");
    if (!g_failed) {
        printf("OVERFLOW queue=audio-stop capacity=8 count=%u overflow_count=%u dropped=0 "
               "first_id=%llu last_id=%llu rejected=1 terminal=failure reason=overflow "
               "terminal_count=1\n",
               (unsigned)audio->count, (unsigned)audio->overflow_count,
               (unsigned long long)audio->items[0].operation_id,
               (unsigned long long)audio->items[7].operation_id);
        printf("OVERFLOW queue=page-apply capacity=8 count=%u overflow_count=%u dropped=0 "
               "rejected=1\n",
               (unsigned)page->count, (unsigned)page->overflow_count);
    }
    expect(lab05_peak_drain_workers(&session, LAB05_PEAK_SCENARIO_OVERLAP,
                                    LAB05_PEAK_CONTROL_PERSISTENT) == 0,
           "drain accepted");
    expect(session.audio_stop.count == 0u && session.page_apply.count == 0u, "queues emptied by drain");
    for (i = 0; i < 8u; i++) {
        const lab05_peak_operation *op = lab05_peak_find_operation(&session, 101u + i);
        expect(op != NULL && op->terminal == LAB05_PEAK_TERMINAL_SUCCESS && op->terminal_count == 1u,
               "audio command terminal");
        op = lab05_peak_find_operation(&session, 201u + i);
        expect(op != NULL && op->terminal == LAB05_PEAK_TERMINAL_SUCCESS && op->terminal_count == 1u,
               "page command terminal");
    }
    if (!g_failed) {
        printf("OVERFLOW_DRAIN accepted_audio=8 accepted_page=8\n");
    }
    return finish();
}

static int test_writer(void)
{
    lab05_peak_session session;
    int y_before;
    unsigned page_before;
    g_failed = 0;
    lab05_peak_session_init(&session);
    expect(lab05_peak_session_apply_transition(&session, LAB05_PEAK_TRIGGER_COVER_DOWN, 7) == 0,
           "cover");
    expect(lab05_peak_animation_advance(&session, 200) == 0, "advance");
    y_before = session.incoming_y;
    page_before = session.page_count;
    expect(session.stage == LAB05_PEAK_STAGE_COVER_DOWN, "animating");
    expect(y_before == 120, "partial y");
    expect(lab05_peak_on_worker_result(&session, 8, LAB05_PEAK_SCENARIO_CANCEL,
                                       LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_TERMINAL_SUCCESS,
                                       "done", 1, 1) == 0,
           "worker result recorded");
    expect(session.writer_rejects >= 1u, "writer rejected");
    expect(session.stage == LAB05_PEAK_STAGE_COVER_DOWN, "stage unchanged");
    expect(session.incoming_y == y_before, "partial input kept");
    expect(session.page_count == page_before, "page not cleared");
    expect(lab05_peak_animation_advance(&session, LAB05_PEAK_COVER_MS) == 0, "finish clock");
    expect(lab05_peak_session_apply_transition(&session, LAB05_PEAK_TRIGGER_COMPLETE, 7) == 0,
           "only session writer completes");
    expect(session.stage == LAB05_PEAK_STAGE_STEADY, "session applied steady");
    if (!g_failed) {
        printf("WRITER stage_after_worker=cover_down partial_y=120 cleared=0 rejected=1 "
               "session_stage_after=steady\n");
    }
    return finish();
}

static int test_terminals(void)
{
    lab05_peak_session session;
    const lab05_peak_operation *op;
    g_failed = 0;
    lab05_peak_session_init(&session);
    expect(lab05_peak_set_terminal(&session, 1, LAB05_PEAK_SCENARIO_OVERLAP,
                                   LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_OP_REPETITION,
                                   LAB05_PEAK_TERMINAL_SUCCESS, "ok") == 0,
           "first");
    expect(lab05_peak_set_terminal(&session, 1, LAB05_PEAK_SCENARIO_OVERLAP,
                                   LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_OP_REPETITION,
                                   LAB05_PEAK_TERMINAL_CANCELLED, "cancel") != 0,
           "second");
    expect(lab05_peak_set_terminal(&session, 1, LAB05_PEAK_SCENARIO_OVERLAP,
                                   LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_OP_REPETITION,
                                   LAB05_PEAK_TERMINAL_TIMED_OUT, "timeout") != 0,
           "timed_out second");
    op = lab05_peak_find_operation(&session, 1);
    expect(op != NULL && op->terminal == LAB05_PEAK_TERMINAL_SUCCESS && op->terminal_count == 1u,
           "sticky success");
    expect(session.duplicate_terminal_rejects == 2u, "two rejects");
    expect(strcmp(lab05_peak_terminal_name(LAB05_PEAK_TERMINAL_SUCCESS), "success") == 0, "success name");
    expect(strcmp(lab05_peak_terminal_name(LAB05_PEAK_TERMINAL_FAILURE), "failure") == 0, "failure name");
    expect(strcmp(lab05_peak_terminal_name(LAB05_PEAK_TERMINAL_CANCELLED), "cancelled") == 0, "cancelled name");
    expect(strcmp(lab05_peak_terminal_name(LAB05_PEAK_TERMINAL_STALE), "stale") == 0, "stale name");
    expect(strcmp(lab05_peak_terminal_name(LAB05_PEAK_TERMINAL_TIMED_OUT), "timed_out") == 0, "timed_out name");
    if (!g_failed) {
        printf("TERMINAL first=success second=rejected terminal_count=1 "
               "outcomes=success,failure,cancelled,stale,timed_out\n");
    }
    return finish();
}

static int test_overlap(int extra, int missing)
{
    lab05_peak_session session;
    unsigned missing_count = 99;
    unsigned duplicate_ids = 99;
    unsigned extra_count = 99;
    unsigned repetitions = missing ? 19u : 20u;
    const lab05_peak_operation *first = NULL;
    size_t i;
    int completeness;
    g_failed = 0;
    lab05_peak_session_init(&session);
    expect(lab05_peak_run_overlap(&session, repetitions) == 0, "overlap run");
    completeness = lab05_peak_overlap_completeness(&session, LAB05_PEAK_OVERLAP_REPETITIONS,
                                                   &missing_count, &duplicate_ids, &extra_count);
    expect(session.max_live_images == 2u, "both images held");
    expect(session.live_images == LAB05_PEAK_STEADY_LIVE_IMAGES, "returned to steady");
    expect(session.running_operations == 0u, "running cleared");
    expect(session.animation_elapsed_ms == 0u, "completed cover clears elapsed");
    expect(session.max_depth_seen >= 2u && session.max_depth_seen <= LAB05_PEAK_MAX_STACK_OR_CALL_DEPTH,
           "depth within probe");
    for (i = 0; i < session.op_count; i++) {
        if (session.ops[i].scenario == LAB05_PEAK_SCENARIO_OVERLAP &&
            session.ops[i].kind == LAB05_PEAK_OP_REPETITION) {
            if (first == NULL) {
                first = &session.ops[i];
            }
            expect(session.ops[i].temp_image_bytes == LAB05_PEAK_IMAGE_PAIR_BYTES, "pair bytes");
            expect(session.ops[i].live_image_count == 2u, "two live at terminal");
            expect(session.ops[i].terminal_count == 1u, "one each");
        }
    }
    expect(first != NULL, "found repetition");
    if (missing) {
        expect(completeness != 0, "19 is incomplete");
        expect(missing_count == 1u, "one missing");
        if (!g_failed) {
            printf("OVERLAP_MISSING repetitions=19 completeness=fail missing=%u\n", missing_count);
        }
        return finish();
    }
    expect(completeness == 0, "20 complete");
    expect(missing_count == 0u && duplicate_ids == 0u && extra_count == 0u, "clean counts");
    if (!extra) {
        if (!g_failed) {
            printf("OVERLAP repetitions=20 completeness=ok missing=0 duplicate_ids=0 extra=0 "
                   "max_live_images=2 peak_bytes=%u live_after=%u\n",
                   LAB05_PEAK_IMAGE_PAIR_BYTES, session.live_images);
        }
        return finish();
    }
    expect(first != NULL, "id");
    if (first != NULL) {
        expect(lab05_peak_set_terminal(&session, first->operation_id, LAB05_PEAK_SCENARIO_OVERLAP,
                                       LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_OP_REPETITION,
                                       LAB05_PEAK_TERMINAL_FAILURE, "alloc") != 0,
               "duplicate terminal");
        expect(first->terminal_count == 1u && first->terminal == LAB05_PEAK_TERMINAL_SUCCESS,
               "original kept");
    }
    completeness = lab05_peak_overlap_completeness(&session, LAB05_PEAK_OVERLAP_REPETITIONS,
                                                   &missing_count, &duplicate_ids, &extra_count);
    expect(completeness != 0, "duplicate fails completeness");
    expect(session.duplicate_terminal_rejects >= 1u, "reject counted");
    if (!g_failed) {
        printf("OVERLAP_DUPLICATE completeness=fail duplicate_terminals=%u terminal_count=1\n",
               session.duplicate_terminal_rejects);
    }
    return finish();
}

static int test_overlap_extra_op(void)
{
    lab05_peak_session session;
    unsigned missing_count = 0;
    unsigned duplicate_ids = 0;
    unsigned extra_count = 0;
    g_failed = 0;
    lab05_peak_session_init(&session);
    expect(lab05_peak_run_overlap(&session, 20) == 0, "run");
    expect(lab05_peak_set_terminal(&session, 9000, LAB05_PEAK_SCENARIO_OVERLAP,
                                   LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_OP_REPETITION,
                                   LAB05_PEAK_TERMINAL_SUCCESS, "ok") == 0,
           "extra op");
    expect(lab05_peak_overlap_completeness(&session, 20, &missing_count, &duplicate_ids,
                                           &extra_count) != 0,
           "extra fails");
    expect(extra_count == 1u, "one extra");
    if (!g_failed) {
        printf("OVERLAP_EXTRA completeness=fail extra=%u\n", extra_count);
    }
    return finish();
}

static int test_replace(void)
{
    lab05_peak_session session;
    lab05_peak_handle old_handle;
    const lab05_peak_operation *success;
    const lab05_peak_operation *failure;
    uint8_t byte = 0;
    g_failed = 0;
    lab05_peak_session_init(&session);
    expect(lab05_peak_run_replace(&session, 100, 0, &old_handle) == 0, "replace success");
    expect(session.max_live_images == 1u, "replace never held both");
    success = lab05_peak_find_operation(&session, 100);
    expect(success != NULL && success->terminal == LAB05_PEAK_TERMINAL_SUCCESS, "success");
    expect(success != NULL && success->terminal_count == 1u, "success count");
    expect(success != NULL && success->temp_image_bytes == LAB05_PEAK_IMAGE_BYTES, "one image");
    expect(success != NULL && success->control == LAB05_PEAK_CONTROL_REPLACE, "replace control");
    expect(lab05_peak_run_replace(&session, 200, 1, &old_handle) == 0, "replace alloc fail");
    failure = lab05_peak_find_operation(&session, 200);
    expect(failure != NULL && failure->terminal == LAB05_PEAK_TERMINAL_FAILURE, "failure");
    expect(failure != NULL && strcmp(failure->reason, "alloc") == 0, "alloc");
    expect(failure != NULL && failure->terminal_count == 1u, "failure count");
    expect(success->operation_id != failure->operation_id, "distinct ids");
    expect(session.images[old_handle.slot].reads == 0, "no read after delete");
    expect(lab05_peak_read_image(&session, old_handle, &byte) != 0, "rejected use");
    expect(session.images[old_handle.slot].reads == 0, "still zero");
    expect(session.max_live_images == 1u, "still one");
    if (!g_failed) {
        printf("REPLACE success_terminal=success failure_terminal=failure failure_reason=alloc "
               "distinct_ids=1 max_live_images=1 reads_after_delete=0\n");
    }
    return finish();
}

static int test_budget(void)
{
    lab05_peak_session session;
    const lab05_peak_operation *too_big;
    unsigned i;
    g_failed = 0;
    printf("CONST canvas_width=%u canvas_height=%u image_side=%u image_bytes=%u "
           "image_pair_bytes=%u cover_ms=%u incoming_y_start=%d incoming_y_end=%d "
           "overlap_repetitions=%u\n",
           LAB05_PEAK_CANVAS_WIDTH, LAB05_PEAK_CANVAS_HEIGHT, LAB05_PEAK_IMAGE_SIDE,
           LAB05_PEAK_IMAGE_BYTES, LAB05_PEAK_IMAGE_PAIR_BYTES, LAB05_PEAK_COVER_MS,
           LAB05_PEAK_INCOMING_Y_START, LAB05_PEAK_INCOMING_Y_END, LAB05_PEAK_OVERLAP_REPETITIONS);
    printf("BUDGET max_live_nodes=%u max_transient_bytes=%u max_persistent_bytes=%u "
           "max_concurrent_operations=%u max_stack_or_call_depth=%u maximum_input_size=%u "
           "steady_live_images=%u audio_stop_queue_capacity=%u page_apply_queue_capacity=%u\n",
           LAB05_PEAK_MAX_LIVE_NODES, LAB05_PEAK_MAX_TRANSIENT_BYTES, LAB05_PEAK_MAX_PERSISTENT_BYTES,
           LAB05_PEAK_MAX_CONCURRENT_OPERATIONS, LAB05_PEAK_MAX_STACK_OR_CALL_DEPTH,
           LAB05_PEAK_MAXIMUM_INPUT_SIZE, LAB05_PEAK_STEADY_LIVE_IMAGES,
           LAB05_PEAK_AUDIO_STOP_QUEUE_CAPACITY, LAB05_PEAK_PAGE_APPLY_QUEUE_CAPACITY);
    lab05_peak_session_init(&session);
    for (i = 0; i < LAB05_PEAK_MAX_CONCURRENT_OPERATIONS; i++) {
        expect(lab05_peak_try_begin_operation(&session) == 0, "begin");
    }
    expect(session.running_operations == LAB05_PEAK_MAX_CONCURRENT_OPERATIONS, "at cap");
    expect(lab05_peak_try_begin_operation(&session) != 0, "fourth rejected");
    expect(session.running_operations == LAB05_PEAK_MAX_CONCURRENT_OPERATIONS, "cap held");
    expect(lab05_peak_submit_pixels(&session, 2, LAB05_PEAK_MAXIMUM_INPUT_SIZE) == 0, "max accepted");
    expect(lab05_peak_submit_pixels(&session, 3, LAB05_PEAK_MAXIMUM_INPUT_SIZE + 1u) != 0, "over rejected");
    too_big = lab05_peak_find_operation(&session, 3);
    expect(too_big != NULL && too_big->terminal == LAB05_PEAK_TERMINAL_FAILURE, "input failure");
    expect(too_big != NULL && strcmp(too_big->reason, "input_too_large") == 0, "reason");
    expect(too_big != NULL && too_big->terminal_count == 1u, "input count");
    if (!g_failed) {
        printf("CONCURRENT running=%u fourth=rejected\n", session.running_operations);
        printf("INPUT accepted=%u rejected=%u terminal=failure reason=input_too_large terminal_count=1\n",
               LAB05_PEAK_MAXIMUM_INPUT_SIZE, LAB05_PEAK_MAXIMUM_INPUT_SIZE + 1u);
    }
    return finish();
}

static int read_file(const char *path, uint8_t *dst, size_t bytes)
{
    FILE *file = fopen(path, "rb");
    size_t got;
    if (file == NULL) {
        fprintf(stderr, "FAIL open %s\n", path);
        return -1;
    }
    got = fread(dst, 1, bytes, file);
    fclose(file);
    return got == bytes ? 0 : -1;
}

static int test_images(const char *path_a, const char *path_b)
{
    lab05_peak_session session;
    lab05_peak_handle second;
    uint8_t *filled;
    uint8_t *from_file;
    char labels[16 * 12];
    unsigned i;
    size_t used = 0;
    g_failed = 0;
    filled = (uint8_t *)malloc(LAB05_PEAK_IMAGE_BYTES);
    from_file = (uint8_t *)malloc(LAB05_PEAK_IMAGE_BYTES);
    expect(filled != NULL && from_file != NULL, "malloc");
    if (filled != NULL && from_file != NULL) {
        lab05_peak_fill_image(filled, LAB05_PEAK_IMAGE_BYTES, 0u);
        expect(read_file(path_a, from_file, LAB05_PEAK_IMAGE_BYTES) == 0, "read a");
        expect(memcmp(filled, from_file, LAB05_PEAK_IMAGE_BYTES) == 0, "image a matches model");
        lab05_peak_fill_image(filled, LAB05_PEAK_IMAGE_BYTES, 1u);
        expect(read_file(path_b, from_file, LAB05_PEAK_IMAGE_BYTES) == 0, "read b");
        expect(memcmp(filled, from_file, LAB05_PEAK_IMAGE_BYTES) == 0, "image b matches model");
    }
    lab05_peak_session_init(&session);
    expect(session.images[session.steady.slot].pixels != NULL, "steady pixels");
    if (filled != NULL && session.images[session.steady.slot].pixels != NULL) {
        lab05_peak_fill_image(filled, LAB05_PEAK_IMAGE_BYTES, 0u);
        expect(memcmp(session.images[session.steady.slot].pixels, filled, LAB05_PEAK_IMAGE_BYTES) == 0,
               "alloc filled image a");
    }
    expect(lab05_peak_alloc_image(&session, 1u, &second) == 0, "second image");
    expect(session.live_images == 2u, "two live");
    expect(session.pixel_bytes == LAB05_PEAK_IMAGE_PAIR_BYTES, "57600");
    expect(lab05_peak_alloc_image(&session, 1u, &second) != 0, "third rejected");
    expect(session.live_images == 2u, "still two");
    expect(lab05_peak_apply_summary_page(&session, 17) != 0, "17 rejected");
    expect(session.page_count == 0u, "page unchanged");
    expect(lab05_peak_apply_summary_page(&session, 16) == 0, "16 applied");
    expect(lab05_peak_apply_summary_page(&session, 16) == 0, "idempotent");
    expect(session.page_count == 16u, "sixteen");
    expect(session.page_count + session.live_images + session.live_roots <= LAB05_PEAK_MAX_LIVE_NODES,
           "nodes");
    labels[0] = '\0';
    for (i = 0; i < 16u; i++) {
        int wrote = snprintf(labels + used, sizeof(labels) - used, "%s%s", (i == 0u) ? "" : ",",
                             session.page[i].label);
        expect(wrote > 0, "label");
        if (wrote > 0) {
            used += (size_t)wrote;
        }
        expect(session.page[i].summary_id == i + 1u, "summary id");
    }
    if (!g_failed) {
        printf("IMAGES bytes=28800 pair=57600 match=1 third=rejected\n");
        printf("PAGE count=16 reject17=1 labels=%s\n", labels);
    }
    free(filled);
    free(from_file);
    return finish();
}

static int trace_has_secret(const char *line)
{
    if (strstr(line, "password") != NULL || strstr(line, "Bearer") != NULL ||
        strstr(line, "PIN") != NULL || strstr(line, "://") != NULL) {
        return 1;
    }
    return 0;
}

static int test_trace(void)
{
    lab05_peak_session session;
    const lab05_peak_operation *op = NULL;
    char line[1024];
    size_t i;
    g_failed = 0;
    lab05_peak_session_init(&session);
    expect(lab05_peak_run_overlap(&session, 1) == 0, "one overlap");
    for (i = 0; i < session.op_count; i++) {
        if (session.ops[i].kind == LAB05_PEAK_OP_REPETITION) {
            op = &session.ops[i];
            break;
        }
    }
    expect(op != NULL, "op");
    if (op != NULL) {
        expect(lab05_peak_format_trace(&session, op->operation_id, line, sizeof(line)) == 0, "format");
        expect(strstr(line, "LAB05_PEAK_TRACE ") == line, "prefix");
        expect(strstr(line, "schema=lab05-peak-trace/v1") != NULL, "schema");
        expect(strstr(line, "scenario=P-overlap") != NULL, "scenario");
        expect(strstr(line, "kind=repetition") != NULL, "kind");
        expect(strstr(line, "control=persistent") != NULL, "control");
        expect(strstr(line, "terminal=success") != NULL, "terminal");
        expect(strstr(line, "terminal_count=1") != NULL, "count");
        expect(strstr(line, "live_image_count=2") != NULL, "live");
        expect(strstr(line, "temp_image_bytes=57600") != NULL, "bytes");
        expect(strstr(line, "lvgl_high_water_bytes=unmeasured") != NULL, "lvgl high");
        expect(strstr(line, "lvgl_largest_free_bytes=unmeasured") != NULL, "lvgl free");
        expect(strstr(line, "internal_largest_free_bytes=unmeasured") != NULL, "internal");
        expect(strstr(line, "psram_largest_free_bytes=unmeasured") != NULL, "psram");
        expect(strstr(line, "anim_frame_us=unmeasured") != NULL, "anim");
        expect(strstr(line, "metrics_provenance=unmeasured") != NULL, "provenance");
        expect(!trace_has_secret(line), "no secrets");
        expect(strlen(line) < 800u, "no pixel payload");
        if (!g_failed) {
            fputs(line, stdout);
        }
    }
    return finish();
}

static int test_contract(void)
{
    g_failed = 0;
    expect(strcmp(lab05_peak_experiment_input_id(0), "P-alloc-fail") == 0, "0");
    expect(strcmp(lab05_peak_experiment_input_id(1), "P-stale-callback") == 0, "1");
    expect(strcmp(lab05_peak_experiment_input_id(2), "P-cancel") == 0, "2");
    expect(lab05_peak_experiment_input_id(3) == NULL, "no fourth");
    expect(lab05_peak_scenario_is_experiment_input(LAB05_PEAK_SCENARIO_ALLOC_FAIL), "alloc in");
    expect(lab05_peak_scenario_is_experiment_input(LAB05_PEAK_SCENARIO_STALE_CALLBACK), "stale in");
    expect(lab05_peak_scenario_is_experiment_input(LAB05_PEAK_SCENARIO_CANCEL), "cancel in");
    expect(!lab05_peak_scenario_is_experiment_input(LAB05_PEAK_SCENARIO_OVERLAP), "overlap out");
    expect(!lab05_peak_scenario_is_experiment_input(LAB05_PEAK_SCENARIO_REPLACE), "replace out");
    if (!g_failed) {
        printf("EXPERIMENT_INPUTS P-alloc-fail,P-stale-callback,P-cancel\n");
        printf("EXPLORATORY P-overlap,P-replace\n");
    }
    return finish();
}

static int count_kind(const lab05_peak_session *session, lab05_peak_scenario scenario,
                      lab05_peak_op_kind kind, lab05_peak_terminal terminal)
{
    size_t i;
    int count = 0;
    for (i = 0; i < session->op_count; i++) {
        const lab05_peak_operation *op = &session->ops[i];
        if (op->scenario == scenario && op->kind == kind && op->terminal == terminal &&
            op->terminal_count == 1u) {
            count++;
        }
    }
    return count;
}

static int test_cover_alloc(void)
{
    lab05_peak_session overlap;
    lab05_peak_session cancel;
    unsigned running;
    g_failed = 0;
    lab05_peak_session_init(&overlap);
    running = overlap.running_operations;
    overlap.inject_alloc_fail = 1;
    expect(lab05_peak_run_overlap(&overlap, 1) != 0, "overlap alloc fails");
    expect(overlap.animation_active == 0, "overlap animation clear");
    expect(overlap.stage == LAB05_PEAK_STAGE_STEADY, "overlap stage restored");
    expect(overlap.running_operations == running, "overlap running restored");
    expect(count_kind(&overlap, LAB05_PEAK_SCENARIO_OVERLAP, LAB05_PEAK_OP_REPETITION,
                      LAB05_PEAK_TERMINAL_FAILURE) == 1,
           "one overlap failure");
    expect(count_kind(&overlap, LAB05_PEAK_SCENARIO_OVERLAP, LAB05_PEAK_OP_WORKER,
                      LAB05_PEAK_TERMINAL_SUCCESS) == 0,
           "no overlap workers");
    lab05_peak_session_init(&cancel);
    running = cancel.running_operations;
    cancel.inject_alloc_fail = 1;
    expect(lab05_peak_run_cancel(&cancel, 11, 12, 13) != 0, "cancel alloc fails");
    expect(cancel.animation_active == 0, "cancel animation clear");
    expect(cancel.stage == LAB05_PEAK_STAGE_STEADY, "cancel stage restored");
    expect(cancel.running_operations == running, "cancel running restored");
    expect(count_kind(&cancel, LAB05_PEAK_SCENARIO_CANCEL, LAB05_PEAK_OP_REPETITION,
                      LAB05_PEAK_TERMINAL_FAILURE) == 1,
           "one cancel failure");
    if (!g_failed) {
        printf("COVER_ALLOC overlap_terminal=failure overlap_count=1 animation_active=0 "
               "running_operations=0 cancel_terminal=failure cancel_count=1\n");
    }
    return finish();
}

static int test_ledger_full(void)
{
    lab05_peak_session full;
    lab05_peak_session open_slot;
    lab05_peak_handle incoming;
    unsigned i;
    const lab05_peak_operation *op;
    g_failed = 0;
    lab05_peak_session_init(&full);
    for (i = 0; i < LAB05_PEAK_MAX_OPERATIONS; i++) {
        expect(lab05_peak_set_terminal(&full, 1000u + i, LAB05_PEAK_SCENARIO_OVERLAP,
                                       LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_OP_OTHER,
                                       LAB05_PEAK_TERMINAL_SUCCESS, "ok") == 0,
               "fill ledger");
    }
    expect(full.op_count == LAB05_PEAK_MAX_OPERATIONS, "ledger full");
    expect(lab05_peak_session_apply_transition(&full, LAB05_PEAK_TRIGGER_COVER_DOWN, 9001) == 0,
           "cover starts");
    expect(lab05_peak_alloc_image(&full, 1u, &incoming) == 0, "incoming allocated");
    full.has_incoming = 1;
    full.incoming = incoming;
    expect(full.live_images == 2u, "two live before complete");
    expect(lab05_peak_animation_advance(&full, LAB05_PEAK_COVER_MS) == 0, "cover elapsed");
    expect(lab05_peak_session_apply_transition(&full, LAB05_PEAK_TRIGGER_COMPLETE, 9001) != 0,
           "complete rejected");
    expect(full.animation_active == 1, "cover not finished");
    expect(full.stage == LAB05_PEAK_STAGE_COVER_DOWN, "stage still cover");
    expect(full.live_images == 2u, "incoming still owned");
    expect(lab05_peak_find_operation(&full, 9001) == NULL, "no silent terminal");
    lab05_peak_session_init(&open_slot);
    for (i = 0; i < LAB05_PEAK_MAX_OPERATIONS - 1u; i++) {
        expect(lab05_peak_set_terminal(&open_slot, 2000u + i, LAB05_PEAK_SCENARIO_OVERLAP,
                                       LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_OP_OTHER,
                                       LAB05_PEAK_TERMINAL_SUCCESS, "ok") == 0,
               "fill all but one");
    }
    expect(lab05_peak_session_apply_transition(&open_slot, LAB05_PEAK_TRIGGER_COVER_DOWN, 9002) == 0,
           "open cover");
    expect(lab05_peak_alloc_image(&open_slot, 1u, &incoming) == 0, "open incoming");
    open_slot.has_incoming = 1;
    open_slot.incoming = incoming;
    expect(lab05_peak_animation_advance(&open_slot, LAB05_PEAK_COVER_MS) == 0, "open elapsed");
    expect(lab05_peak_session_apply_transition(&open_slot, LAB05_PEAK_TRIGGER_COMPLETE, 9002) == 0,
           "reserved slot completes");
    op = lab05_peak_find_operation(&open_slot, 9002);
    expect(op != NULL && op->terminal == LAB05_PEAK_TERMINAL_SUCCESS && op->terminal_count == 1u,
           "stored success");
    expect(open_slot.animation_active == 0, "completed cover is idle");
    expect(lab05_peak_delete_image(&open_slot, incoming) == 0, "release incoming");
    expect(open_slot.live_images == LAB05_PEAK_STEADY_LIVE_IMAGES, "no leaked incoming");
    if (!g_failed) {
        printf("LEDGER_FULL complete=rejected animation_active=1 stage=cover_down "
               "live_images=2 terminal=absent\n");
        printf("LEDGER_SLOT complete=accepted terminal=success live_after_release=1\n");
    }
    return finish();
}

static int test_view_hold(void)
{
    lab05_peak_session held;
    lab05_peak_session immediate;
    lab05_peak_handle handle;
    uint8_t *pixels;
    uint8_t byte = 0;
    unsigned bytes_before;
    g_failed = 0;
    lab05_peak_session_init(&held);
    handle = held.steady;
    pixels = held.images[handle.slot].pixels;
    bytes_before = held.pixel_bytes;
    expect(pixels != NULL, "steady pixels");
    expect(lab05_peak_retain_view(&held, handle) == 0, "retain");
    expect(lab05_peak_delete_image(&held, handle) == 0, "logical delete");
    expect(held.images[handle.slot].pixels == pixels, "buffer still allocated");
    expect(held.pixel_bytes == bytes_before, "bytes stay until release");
    expect(lab05_peak_read_image(&held, handle, &byte) != 0, "read rejected");
    expect(held.images[handle.slot].reads == 0, "no pixel read");
    expect(lab05_peak_release_view(&held, handle) == 0, "release after canvas delete");
    expect(held.images[handle.slot].pixels == NULL, "freed after release");
    expect(held.pixel_bytes == 0u, "bytes released");
    lab05_peak_session_init(&immediate);
    handle = immediate.steady;
    expect(lab05_peak_delete_image(&immediate, handle) == 0, "delete without view");
    expect(immediate.images[handle.slot].pixels == NULL, "freed immediately");
    if (!g_failed) {
        printf("VIEW_HOLD retained_pixels=1 reads_while_held=0 freed_after_release=1 "
               "immediate_free=1\n");
    }
    return finish();
}

static int test_heap_provenance(void)
{
    g_failed = 0;
    expect(lab05_peak_heap_metrics_are_instrumented(0) == 0, "memset is not a sample");
    expect(lab05_peak_heap_metrics_are_instrumented(1) == 1, "capture is instrumented");
    if (!g_failed) {
        printf("HEAP captured=instrumented uncaptured=unmeasured\n");
    }
    return finish();
}

int main(int argc, char **argv)
{
    const char *mode = (argc > 1) ? argv[1] : "handles";
    if (strcmp(mode, "handles") == 0) {
        return test_handles();
    }
    if (strcmp(mode, "alloc-fail") == 0) {
        return test_alloc_fail();
    }
    if (strcmp(mode, "stale") == 0) {
        return test_stale();
    }
    if (strcmp(mode, "cancel") == 0) {
        return test_cancel();
    }
    if (strcmp(mode, "overflow") == 0) {
        return test_overflow();
    }
    if (strcmp(mode, "writer") == 0) {
        return test_writer();
    }
    if (strcmp(mode, "terminals") == 0) {
        return test_terminals();
    }
    if (strcmp(mode, "overlap") == 0) {
        return test_overlap(0, 0);
    }
    if (strcmp(mode, "overlap-missing") == 0) {
        return test_overlap(0, 1);
    }
    if (strcmp(mode, "overlap-duplicate") == 0) {
        return test_overlap(1, 0);
    }
    if (strcmp(mode, "overlap-extra") == 0) {
        return test_overlap_extra_op();
    }
    if (strcmp(mode, "replace") == 0) {
        return test_replace();
    }
    if (strcmp(mode, "budget") == 0) {
        return test_budget();
    }
    if (strcmp(mode, "images") == 0) {
        if (argc < 4) {
            fprintf(stderr, "FAIL images requires two fixture paths\n");
            return 1;
        }
        return test_images(argv[2], argv[3]);
    }
    if (strcmp(mode, "trace") == 0) {
        return test_trace();
    }
    if (strcmp(mode, "contract") == 0) {
        return test_contract();
    }
    if (strcmp(mode, "cover-alloc") == 0) {
        return test_cover_alloc();
    }
    if (strcmp(mode, "ledger-full") == 0) {
        return test_ledger_full();
    }
    if (strcmp(mode, "view-hold") == 0) {
        return test_view_hold();
    }
    if (strcmp(mode, "heap-provenance") == 0) {
        return test_heap_provenance();
    }
    fprintf(stderr, "FAIL unknown mode %s\n", mode);
    return 1;
}
