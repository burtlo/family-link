#include "lab11_capture.h"
#include "lab11_send.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        return 1; \
    } \
} while (0)

static lab11_capture_info_t info_one(void)
{
    lab11_capture_info_t info;
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
    return info;
}

static int test_sha256(void)
{
    uint8_t digest[32];
    char hex[65];
    lab11_sha256((const uint8_t *)"abc", 3, digest);
    lab11_sha256_hex(digest, hex);
    CHECK(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    return 0;
}

static int test_hashpayload(void)
{
    uint8_t payload[LAB11_PAYLOAD_LEN];
    uint8_t digest[32];
    char hex[65];
    lab11_capture_synthesize(payload, sizeof(payload));
    lab11_sha256(payload, sizeof(payload), digest);
    lab11_sha256_hex(digest, hex);
    printf("%s\n", hex);
    return 0;
}

static int test_boot(void)
{
    char line[160];
    lab11_format_boot(line, sizeof(line), false, "lab11-fw-manual-v1", "manual", 0);
    CHECK(strstr(line, "LAB11_READY") == NULL);
    CHECK(strstr(line, "status=display_start_failed") != NULL);
    lab11_format_boot(line, sizeof(line), true, "lab11-fw-scripted-v1", "scripted", 1);
    CHECK(strstr(line, "LAB11_READY build=lab11-fw-scripted-v1 mode=scripted fixture=lab11-send/v1 harness=1") != NULL);
    lab11_format_harness(line, sizeof(line), true);
    CHECK(strcmp(line, "LAB11_HARNESS_DONE status=complete") == 0);
    lab11_format_harness(line, sizeof(line), false);
    CHECK(strstr(line, "status=complete") == NULL);
    return 0;
}

static int test_ownership(void)
{
    lab11_capture_t capture;
    lab11_session_t session;
    lab11_capture_info_t info = info_one();
    lab11_worker_event_t event;
    CHECK(lab11_capture_create(&capture, LAB11_PAYLOAD_LEN, &info));
    lab11_session_init(&session, LAB11_WRITER_ID);
    CHECK(lab11_op_begin(&session, &capture, LAB11_WRITER_ID, 40, "m-own", LAB11_COND_ARCHIVE,
                         LAB11_CHUNK_1024, false));
    CHECK(session.borrow.held);
    CHECK(capture.borrows == 1u);
    snprintf(capture.info.recipients[0], LAB11_RECIPIENT_LEN, "other");
    CHECK(strcmp(session.frozen_recipients[0], "kin-1") == 0);
    CHECK(memcmp(session.frozen_sha, capture.content_sha256, 32) == 0);
    CHECK(!lab11_capture_free(&capture));
    memset(&event, 0, sizeof(event));
    event.kind = LAB11_EV_CANCEL;
    event.operation_id = 40;
    CHECK(lab11_writer_apply(&session, LAB11_WRITER_ID, &event));
    CHECK(!session.borrow.held);
    CHECK(session.terminal_count == 1u);
    CHECK(session.live_buffers == 0u);
    CHECK(session.sketch_len == 0u);
    CHECK(session.pcm_len == 0u);
    CHECK(capture.bytes != NULL);
    CHECK(!lab11_writer_apply(&session, LAB11_WRITER_ID, &event) || session.late_responses == 1u);
    CHECK(session.terminal_count == 1u);
    CHECK(session.server_accepted == 0u);
    CHECK(lab11_capture_free(&capture));
    CHECK(capture.bytes == NULL);
    return 0;
}

static int test_progress(void)
{
    lab11_session_t session;
    uint32_t generation;
    uint32_t exchanges;
    lab11_session_init(&session, LAB11_WRITER_ID);
    generation = session.scene_generation;
    exchanges = session.exchange_count;
    CHECK(lab11_set_progress(&session, LAB11_WRITER_ID, "uploading", LAB11_LEVEL_PROPERTY));
    CHECK(session.scene_generation == generation);
    CHECK(session.progress_level == 0);
    CHECK(session.exchange_count == exchanges);
    CHECK(!lab11_set_progress(&session, LAB11_WRITER_ID, "replace", LAB11_LEVEL_SCENE));
    CHECK(!lab11_set_progress(&session, LAB11_WRITER_ID, "subtree", LAB11_LEVEL_SUBTREE));
    CHECK(session.scene_generation == generation);
    CHECK(strcmp(session.progress_text, "uploading") == 0);
    CHECK(lab11_request_cancel(&session, 1));
    CHECK(session.exchange_count == exchanges);
    CHECK(!lab11_try_park_checking(&session));
    CHECK(!lab11_checking_without_terminal(&session));
    CHECK(lab11_set_active_scenario(&session, LAB11_WRITER_ID, "D-one"));
    CHECK(strstr(session.expected_text, "accepted is 1") != NULL);
    CHECK(strstr(session.expected_text, "Sent") == NULL);
    return 0;
}

static int test_overflow_and_writer(void)
{
    lab11_capture_t capture;
    lab11_session_t session;
    lab11_capture_info_t info = info_one();
    lab11_worker_event_t event;
    int i;
    CHECK(lab11_capture_create(&capture, LAB11_PAYLOAD_LEN, &info));
    lab11_session_init(&session, LAB11_WRITER_ID);
    CHECK(lab11_op_begin(&session, &capture, LAB11_WRITER_ID, 41, "m-q", LAB11_COND_ARCHIVE,
                         LAB11_CHUNK_1024, false));
    memset(&event, 0, sizeof(event));
    event.kind = LAB11_EV_PART;
    event.operation_id = 41;
    CHECK(!lab11_writer_apply(&session, LAB11_WRITER_ID + 1u, &event));
    CHECK(session.terminal_count == 0u);
    CHECK(session.queue_count == 0u);
    for (i = 0; i < 9; i++) {
        lab11_part_desc_t desc;
        desc.sequence = (uint32_t)i;
        desc.offset = 0;
        desc.length = 1;
        lab11_admit_part(&session, LAB11_WRITER_ID, &desc);
    }
    CHECK(session.queue_count == LAB11_PART_QUEUE_CAPACITY);
    CHECK(session.overflow_count == 1u);
    CHECK(session.terminal == LAB11_TERM_FAILURE);
    CHECK(strcmp(session.reason, "overflow") == 0);
    CHECK(session.terminal_count == 1u);
    CHECK(session.live_buffers == 0u);
    CHECK(!session.borrow.held);
    event.kind = LAB11_EV_PART;
    event.operation_id = 41;
    CHECK(lab11_writer_apply(&session, LAB11_WRITER_ID, &event));
    CHECK(session.late_responses == 1u);
    CHECK(session.terminal_count == 1u);
    CHECK(lab11_capture_free(&capture));
    return 0;
}

static int test_account(void)
{
    lab11_capture_t capture;
    lab11_session_t session;
    lab11_capture_info_t info = info_one();
    lab11_worker_event_t event;
    CHECK(lab11_capture_create(&capture, LAB11_PAYLOAD_LEN, &info));
    lab11_session_init(&session, LAB11_WRITER_ID);
    CHECK(lab11_op_begin(&session, &capture, LAB11_WRITER_ID, 42, "m-a", LAB11_COND_ARCHIVE,
                         LAB11_CHUNK_1024, false));
    CHECK(session.staging_bytes == 1024u);
    CHECK(session.transport_bytes == 0u);
    CHECK(session.transport_view_bytes == 1024u);
    CHECK(session.retry_extra_bytes == 0u);
    CHECK(session.response_bytes == 512u);
    CHECK(session.retained_input_bytes == LAB11_PAYLOAD_LEN);
    CHECK(session.full_payload_duplicate_bytes == 0u);
    CHECK(session.peak_calculated_bytes == LAB11_PAYLOAD_LEN + 1024u + 512u);
    CHECK(session.staging_bytes < session.frozen_len);
    memset(&event, 0, sizeof(event));
    event.kind = LAB11_EV_CANCEL;
    event.operation_id = 42;
    CHECK(lab11_writer_apply(&session, LAB11_WRITER_ID, &event));
    CHECK(lab11_op_begin(&session, &capture, LAB11_WRITER_ID, 43, "m-b", LAB11_COND_ARCHIVE,
                         LAB11_CHUNK_4096, true));
    CHECK(session.staging_bytes == 4096u);
    CHECK(session.transport_bytes == 4096u);
    CHECK(session.full_payload_duplicate_bytes == 0u);
    CHECK(session.peak_calculated_bytes == LAB11_PAYLOAD_LEN + 4096u + 4096u + 512u);
    CHECK(session.staging_bytes < session.frozen_len);
    event.operation_id = 43;
    CHECK(lab11_writer_apply(&session, LAB11_WRITER_ID, &event));
    CHECK(session.live_buffers == 0u);
    CHECK(lab11_capture_free(&capture));
    return 0;
}

#ifdef _WIN32
typedef struct {
    int port;
} socket_ctx_t;

static int socket_exchange(void *opaque, const lab11_request_t *request, lab11_response_t *response)
{
    socket_ctx_t *ctx = (socket_ctx_t *)opaque;
    SOCKET sock;
    struct sockaddr_in addr;
    char header[768];
    char raw[4096];
    int wrote;
    int received = 0;
    char *split;
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        response->transport_error = true;
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)ctx->port);
    addr.sin_addr.s_addr = htonl(0x7f000001);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        closesocket(sock);
        response->transport_error = true;
        return -1;
    }
    wrote = snprintf(header, sizeof(header),
                     "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n"
                     "Content-Length: %u\r\nX-Client-Message-Id: %s\r\nX-Operation-Id: %u\r\n"
                     "X-Sequence: %u\r\nX-Offset: %u\r\nX-Chunk-Bytes: %u\r\nX-Content-SHA256: %s\r\n\r\n",
                     request->method[0] ? request->method : "GET",
                     request->path[0] ? request->path : "/",
                     request->body_len, request->client_message_id, request->operation_id,
                     request->sequence, request->offset, request->chunk_bytes,
                     request->content_sha256_hex);
    if (send(sock, header, wrote, 0) != wrote) {
        closesocket(sock);
        response->transport_error = true;
        return -1;
    }
    if (request->body != NULL && request->body_len > 0) {
        if (send(sock, (const char *)request->body, (int)request->body_len, 0) != (int)request->body_len) {
            closesocket(sock);
            response->transport_error = true;
            return -1;
        }
    }
    while (received < (int)sizeof(raw) - 1) {
        int n = recv(sock, raw + received, (int)sizeof(raw) - 1 - received, 0);
        if (n <= 0) {
            break;
        }
        received += n;
    }
    closesocket(sock);
    raw[received] = '\0';
    split = strstr(raw, "\r\n\r\n");
    if (split == NULL || received < 12) {
        response->transport_error = true;
        return -1;
    }
    response->http_status = atoi(raw + 9);
    split += 4;
    response->body_len = (size_t)strlen(split);
    if (response->body_len >= sizeof(response->body)) {
        response->body_len = sizeof(response->body) - 1u;
    }
    memcpy(response->body, split, response->body_len);
    response->body[response->body_len] = '\0';
    response->lost = response->http_status == 204 || response->body_len == 0;
    return 0;
}

static void trace_puts(void *ctx, const char *line)
{
    (void)ctx;
    puts(line);
}

static int test_scripted(void)
{
    lab11_capture_t capture;
    lab11_session_t session;
    lab11_capture_info_t info = info_one();
    lab11_script_input_t input;
    lab11_transport_t transport;
    socket_ctx_t ctx;
    const char *port_text = getenv("LAB11_PORT");
    WSADATA wsa;
    CHECK(port_text != NULL);
    CHECK(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    ctx.port = atoi(port_text);
    memset(&transport, 0, sizeof(transport));
    transport.exchange = socket_exchange;
    transport.ctx = &ctx;
    transport.copies_body = false;
    memset(&input, 0, sizeof(input));
    input.active_condition = "accept-on-archive-commit";
    input.offline_work = "discarded";
    input.restart_status = "open";
    input.resumed0 = 1;
    input.resumed1 = 2;
    CHECK(lab11_capture_create(&capture, LAB11_PAYLOAD_LEN, &info));
    lab11_session_init(&session, LAB11_WRITER_ID);
    if (!lab11_run_scripted(&session, &capture, &transport, &input, trace_puts, NULL)) {
        fprintf(stderr, "scripted runner returned false terminal=%s reason=%s op=%u\n",
                lab11_terminal_name(session.terminal), session.reason, session.operation_id);
        lab11_capture_free(&capture);
        return 1;
    }
    CHECK(strcmp(session.displayed_condition, "accept-on-archive-commit") == 0);
    CHECK(!lab11_checking_without_terminal(&session));
    CHECK(capture.borrows == 0u);
    CHECK(lab11_capture_free(&capture));
    WSACleanup();
    return 0;
}
#endif

int main(int argc, char **argv)
{
    const char *name;
    if (argc < 2) {
        fprintf(stderr, "usage: model_test <case>\n");
        return 2;
    }
    name = argv[1];
    if (strcmp(name, "sha256") == 0) {
        return test_sha256();
    }
    if (strcmp(name, "hashpayload") == 0) {
        return test_hashpayload();
    }
    if (strcmp(name, "boot") == 0) {
        return test_boot();
    }
    if (strcmp(name, "ownership") == 0) {
        return test_ownership();
    }
    if (strcmp(name, "progress") == 0) {
        return test_progress();
    }
    if (strcmp(name, "overflow") == 0) {
        return test_overflow_and_writer();
    }
    if (strcmp(name, "account") == 0) {
        return test_account();
    }
#ifdef _WIN32
    if (strcmp(name, "scripted") == 0) {
        return test_scripted();
    }
#endif
    fprintf(stderr, "unknown case %s\n", name);
    return 2;
}
