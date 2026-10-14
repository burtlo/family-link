/* Actual LVGL click, session, and HTTP provider. RTOS/HTTP adapters are explicit. */
#define main c0_regression_main
#include "test_carousel_baseline.c"
#undef main
#define main startup_regression_main
#include "../test_http_startup_native.c"
#undef main

static void clicked(void)
{
    owner_pumping = true;
    lv_obj_send_event(carousel.count_lab, LV_EVENT_CLICKED, NULL);
    owner_pumping = false;
}
static void owner_step(void)
{
    owner_pumping = true;
    lab02_carousel_pump(&carousel);
    owner_pumping = false;
}
static void broker(void)
{
    assert(!owner_pumping);
    lab02_page_provider_http_service_initial_retry();
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    scenario = argv[1]; artifact_dir = argv[2];
    lv_init();
    lv_display_t *display = lv_display_create(320, 240);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(display, drawbuffer, NULL, sizeof(drawbuffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    assert(lab02_carousel_init(&carousel));
    fail_task = 1;
    lab02_page_provider_http_start_io();
    for (int i = 0; i < 6; i++) owner_step();
    assert(lab02_session_snapshot().load_stage == LAB02_LOAD_ERROR);
    assert(strstr(lv_label_get_text(carousel.count_lab), "inbox unavailable"));
    assert(strstr(lv_label_get_text(carousel.count_lab), "tap to retry"));
    lab02_fetch_req_t original; /* Preserve a stale completion identity. */
    original.session_id = lab02_session_generation(); original.op_id = 1;
    original.page_index = 0;
    unsigned before = task_calls;
    lab02_host_hold_admission(true);
    int64_t start = esp_timer_get_time();
    clicked(); clicked(); owner_step(); broker();
    printf("F008_CLICK elapsed_us=%lld lock_held=1 task_calls=%u\n",
           (long long)(esp_timer_get_time() - start), task_calls);
    assert(task_calls == before); /* Returned while the producer mutex remains held. */
    assert(lab02_session_metrics().fetch_retry == 0);
    lab02_host_hold_admission(false);
    if (!strcmp(scenario, "retry-reset-pending")) {
        lab02_session_reset();
        for (int i = 0; i < 6; i++) owner_step();
        broker(); assert(task_calls == before);
        assert(lab02_session_metrics().fetch_retry == 0);
        puts("F008_RUNTIME scenario=retry-reset-pending PASS"); return 0;
    }
    for (int i = 0; i < 3; i++) owner_step();
    assert(lab02_session_snapshot().load_stage == LAB02_LOAD_SHELL);
    assert(lab02_session_metrics().fetch_retry == 1);
    lab02_fetch_req_t retry; assert(lab02_session_http_initial_request(&retry));
    assert(retry.attempt == 2 && retry.op_id != original.op_id);
    assert(task_calls == before); /* Owner pump never creates tasks. */
    if (!strcmp(scenario, "retry-reset-admitted")) {
        lab02_session_reset();
        broker(); assert(task_calls == before);
        for (int i = 0; i < 6; i++) owner_step();
        lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, retry.session_id, retry.op_id, 0, NULL, true);
        owner_step(); assert(lab02_session_snapshot().load_stage == LAB02_LOAD_SHELL);
        puts("F008_RUNTIME scenario=retry-reset-admitted PASS"); return 0;
    }

    if (!strcmp(scenario, "retry-boot-fail")) fail_task = task_calls + 1;
    else if (!strcmp(scenario, "retry-background-fail")) fail_task = task_calls + 2;
    else fail_task = 0;
    broker(); broker(); assert(task_calls == before + 1);
    if (fail_task != (int)task_calls) { assert(boot_entry); boot_entry(boot_arg); }
    for (int i = 0; i < 15; i++) owner_step();
    lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, original.session_id, original.op_id,
                                      0, NULL, true);
    owner_step();
    if (fail_task) {
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_ERROR);
        assert(strstr(lv_label_get_text(carousel.count_lab), "inbox unavailable"));
        assert(strstr(lv_label_get_text(carousel.count_lab), "tap to retry"));
        if (!strcmp(scenario, "retry-background-fail")) assert(lab02_inbox_model_item(carousel.provider.model, 0));
        unsigned count = task_calls;
        for (int i = 0; i < 120; i++) { owner_step(); broker(); }
        assert(task_calls == count); /* failure does not create automatic retry */
    } else {
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_READY);
        assert(lab02_inbox_model_item(carousel.provider.model, 0));
        assert(lab02_host_fetch_queue_depth() == 1);
    }
    if (!strcmp(scenario, "retry-retained-worker")) {
        unsigned prior = task_calls;
        lab02_session_reset();
        for (int i = 0; i < 12; i++) owner_step();
        assert(lab02_page_provider_http_init(&carousel.provider));
        fail_task = task_calls + 1;
        lab02_page_provider_http_start_io();
        for (int i = 0; i < 12; i++) owner_step();
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_ERROR);
        fail_task = 0;
        clicked(); for (int i = 0; i < 3; i++) owner_step(); broker();
        assert(task_calls == prior + 2);
        boot_entry(boot_arg);
        for (int i = 0; i < 12; i++) owner_step();
        assert(task_calls == prior + 2); /* usable background worker retained */
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_READY);
    }
    checkpoint("retry-terminal");
    printf("F008_RUNTIME scenario=%s PASS\n", scenario);
    return 0;
}
