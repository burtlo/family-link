#include "lab11_fw.h"

#include "lab11_capture.h"
#include "lab11_send.h"
#include "lab11_sha256.h"
#include "lab11_station.h"
#include "sdkconfig.h"

#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *SCENARIOS[] = {
    "D-one", "D-drop-ack", "D-drop-receipt", "D-full", "D-cancel",
    "D-many", "D-invalid-recipient", "D-offline", "D-push-fail", "D-restart"
};

typedef struct {
    lv_obj_t *root;
    lv_obj_t *progress;
    lv_obj_t *phase;
    lv_obj_t *expected;
    lv_obj_t *chunk;
    lv_timer_t *timer;
    lab11_session_t session;
    lab11_capture_t capture;
    bool scripted;
    bool harness_started;
    int scenario_index;
} lab11_ui_t;

static lab11_ui_t ui;

static void fw_trace(void *ctx, const char *line)
{
    (void)ctx;
    printf("%s\n", line);
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    lab11_response_t *response;
    size_t room;
    size_t copy;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->user_data == NULL || event->data_len <= 0) {
        return ESP_OK;
    }
    response = (lab11_response_t *)event->user_data;
    room = sizeof(response->body) - 1u - response->body_len;
    copy = (size_t)event->data_len < room ? (size_t)event->data_len : room;
    if (copy > 0) {
        memcpy(response->body + response->body_len, event->data, copy);
        response->body_len += copy;
        response->body[response->body_len] = '\0';
    }
    return ESP_OK;
}

static int http_exchange(void *ctx, const lab11_request_t *request, lab11_response_t *response)
{
    char url[192];
    esp_http_client_config_t config = {0};
    esp_http_client_handle_t client;
    esp_err_t err;
    (void)ctx;
    snprintf(url, sizeof(url), "http://%s:%d%s", CONFIG_LAB11_SERVER_HOST, CONFIG_LAB11_SERVER_PORT,
             request->path[0] ? request->path : "/");
    config.url = url;
    config.timeout_ms = 1000;
    config.event_handler = http_event;
    config.user_data = response;
    config.method = strcmp(request->method, "POST") == 0 ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    client = esp_http_client_init(&config);
    if (client == NULL) {
        response->transport_error = true;
        return -1;
    }
    if (request->client_message_id[0] != '\0') {
        esp_http_client_set_header(client, "X-Client-Message-Id", request->client_message_id);
    }
    if (request->content_sha256_hex[0] != '\0') {
        esp_http_client_set_header(client, "X-Content-SHA256", request->content_sha256_hex);
    }
    {
        char sequence[16];
        char operation[16];
        snprintf(sequence, sizeof(sequence), "%u", (unsigned)request->sequence);
        snprintf(operation, sizeof(operation), "%u", (unsigned)request->operation_id);
        esp_http_client_set_header(client, "X-Sequence", sequence);
        esp_http_client_set_header(client, "X-Operation-Id", operation);
    }
    if (request->body != NULL && request->body_len > 0) {
        esp_http_client_set_post_field(client, (const char *)request->body, (int)request->body_len);
    }
    err = esp_http_client_perform(client);
    response->http_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        response->transport_error = true;
        return -1;
    }
    response->lost = response->http_status == 204 || response->body_len == 0;
    return 0;
}

static void refresh_labels(void)
{
    lv_label_set_text(ui.progress, ui.session.progress_text);
    lv_label_set_text(ui.phase, ui.session.phase_text);
    lv_label_set_text(ui.expected, ui.session.expected_text);
    lv_label_set_text_fmt(ui.chunk, "chunk probe %u  queue 8", (unsigned)ui.session.chunk_bytes);
}

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    refresh_labels();
}

static void harness_task(void *arg)
{
    lab11_transport_t transport;
    lab11_script_input_t input;
    char line[80];
    bool ok;
    (void)arg;
    memset(&transport, 0, sizeof(transport));
    transport.exchange = http_exchange;
    transport.copies_body = true;
    memset(&input, 0, sizeof(input));
    input.active_condition = "accept-on-archive-commit";
    input.offline_work = "discarded";
    input.restart_status = "open";
    input.resumed0 = 1;
    input.resumed1 = 2;
    input.copies_body = true;
    if (!lab11_station_join()) {
        printf("LAB11_STATION status=join_failed\n");
        lab11_format_harness(line, sizeof(line), false);
        printf("%s\n", line);
        vTaskDelete(NULL);
        return;
    }
    printf("LAB11_STATION status=joined\n");
    lab11_station_set_up(true);
    ok = lab11_run_scripted(&ui.session, &ui.capture, &transport, &input, fw_trace, NULL);
    lab11_format_harness(line, sizeof(line), ok);
    printf("%s\n", line);
    vTaskDelete(NULL);
}

static void start_harness_once(void)
{
    if (ui.harness_started) {
        return;
    }
    ui.harness_started = true;
    xTaskCreate(harness_task, "lab11_send", 16384, NULL, 4, NULL);
}

static void on_next(lv_event_t *event)
{
    (void)event;
    if (ui.scripted) {
        return;
    }
    ui.scenario_index = (ui.scenario_index + 1) % (int)(sizeof(SCENARIOS) / sizeof(SCENARIOS[0]));
    lab11_set_active_scenario(&ui.session, LAB11_WRITER_ID, SCENARIOS[ui.scenario_index]);
    refresh_labels();
}

static void on_chunk(lv_event_t *event)
{
    uint32_t next;
    (void)event;
    if (ui.scripted) {
        return;
    }
    next = ui.session.chunk_bytes == LAB11_CHUNK_1024 ? LAB11_CHUNK_4096 : LAB11_CHUNK_1024;
    lab11_set_chunk_probe(&ui.session, LAB11_WRITER_ID, next);
    refresh_labels();
}

static void on_cancel(lv_event_t *event)
{
    (void)event;
    lab11_request_cancel(&ui.session, ui.session.operation_id);
}

static void on_run(lv_event_t *event)
{
    (void)event;
    if (ui.scripted) {
        return;
    }
    start_harness_once();
}

/* 320x240 canvas, 12 px margin, 296x216 well. Body is Montserrat 16.
 * Built-in LVGL Montserrat is not weight 600 (UI-OPEN-001).
 * Buttons are 144x48, at least the 101x48 primary minimum. */
#define FL_MARGIN 12
#define FL_WELL_W 296
#define FL_WELL_H 216
#define FL_BTN_W 144
#define FL_BTN_H 48
#define FL_GAP 8

static lv_obj_t *add_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL) {
        return NULL;
    }
    lv_obj_set_width(label, FL_WELL_W);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *add_button(lv_obj_t *parent, const char *text, int x, int y, lv_event_cb_t cb)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;
    if (button == NULL) {
        return NULL;
    }
    lv_obj_set_size(button, FL_BTN_W, FL_BTN_H);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_pad_all(button, 0, 0);
    label = lv_label_create(button);
    if (label == NULL) {
        return NULL;
    }
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);
    return button;
}

bool lab11_fw_init(lv_obj_t *screen, const char *mode)
{
    lab11_capture_info_t info;
    char hash_text[128];
    char hex[65];
    if (screen == NULL || mode == NULL) {
        return false;
    }
    memset(&ui, 0, sizeof(ui));
    ui.scripted = strcmp(mode, "scripted") == 0;
    memset(&info, 0, sizeof(info));
    info.accepted_point_count = 128;
    info.duration_cap_ms = 180000;
    info.logical_w = 120;
    info.logical_h = 120;
    info.capture_queue_capacity = 16;
    info.sketch_len = 1200;
    info.pcm_len = 3200;
    info.recipient_count = 1;
    snprintf(info.recipients[0], LAB11_RECIPIENT_LEN, "kin-1");
    if (!lab11_capture_create(&ui.capture, LAB11_PAYLOAD_LEN, &info)) {
        return false;
    }
    lab11_session_init(&ui.session, LAB11_WRITER_ID);
    lab11_set_active_scenario(&ui.session, LAB11_WRITER_ID, "D-one");
    snprintf(ui.session.displayed_condition, sizeof(ui.session.displayed_condition),
             "accept-on-archive-commit");
    lab11_sha256_hex(ui.capture.content_sha256, hex);
    hex[12] = '\0';
    lab11_station_init();
    lv_obj_set_style_pad_all(screen, 0, 0);
    ui.root = lv_obj_create(screen);
    if (ui.root == NULL) {
        return false;
    }
    lv_obj_set_size(ui.root, 320, 240);
    lv_obj_set_pos(ui.root, 0, 0);
    lv_obj_set_style_pad_all(ui.root, 0, 0);
    lv_obj_set_style_border_width(ui.root, 0, 0);
    lv_obj_remove_flag(ui.root, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_obj_t *well = lv_obj_create(ui.root);
        lv_obj_t *text;
        int button_y;
        if (well == NULL) {
            return false;
        }
        lv_obj_set_pos(well, FL_MARGIN, FL_MARGIN);
        lv_obj_set_size(well, FL_WELL_W, FL_WELL_H);
        lv_obj_set_style_pad_all(well, 0, 0);
        lv_obj_set_style_border_width(well, 0, 0);
        lv_obj_remove_flag(well, LV_OBJ_FLAG_SCROLLABLE);
        text = lv_obj_create(well);
        if (text == NULL) {
            return false;
        }
        lv_obj_set_pos(text, 0, 0);
        lv_obj_set_size(text, FL_WELL_W, FL_WELL_H - (FL_BTN_H * 2 + FL_GAP) - FL_GAP);
        lv_obj_set_style_pad_all(text, 0, 0);
        lv_obj_set_style_border_width(text, 0, 0);
        lv_obj_set_style_bg_opa(text, LV_OPA_TRANSP, 0);
        lv_obj_set_layout(text, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(text, 0, 0);
        snprintf(hash_text, sizeof(hash_text), "lab11-send/v1  %s", hex);
        button_y = FL_WELL_H - (FL_BTN_H * 2 + FL_GAP);
        if (add_label(text, hash_text) == NULL
            || add_label(text, "128 points, 180000 ms, 120x120, queue 16") == NULL
            || add_label(text, "accept-on-archive-commit") == NULL
            || (ui.expected = add_label(text, ui.session.expected_text)) == NULL
            || (ui.chunk = add_label(text, "chunk probe 1024  queue 8")) == NULL
            || (ui.progress = add_label(text, "idle")) == NULL
            || (ui.phase = add_label(text, "idle")) == NULL
            || add_button(well, "Next", 0, button_y, on_next) == NULL
            || add_button(well, "Chunk", FL_BTN_W + FL_GAP, button_y, on_chunk) == NULL
            || add_button(well, "Run", 0, button_y + FL_BTN_H + FL_GAP, on_run) == NULL
            || add_button(well, "Cancel", FL_BTN_W + FL_GAP, button_y + FL_BTN_H + FL_GAP, on_cancel) == NULL) {
            return false;
        }
    }
    ui.timer = lv_timer_create(tick_cb, 100, NULL);
    return ui.timer != NULL;
}

void lab11_fw_start(void)
{
    if (!ui.scripted) {
        return;
    }
    if (!lab11_station_prepare_transport()) {
        printf("LAB11_INIT_FAILED status=transport_stack\n");
        return;
    }
    start_harness_once();
}
