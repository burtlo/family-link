#include "probe.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{

int g_failures = 0;

void check(bool cond, const char *text)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", text);
        g_failures += 1;
    }
}

bool load_file(const char *path, char *buffer, int capacity, int *length)
{
    FILE *file = fopen(path, "rb");
    int n;
    if (file == 0 || buffer == 0 || capacity <= 0) {
        return false;
    }
    n = (int)fread(buffer, 1, (size_t)(capacity - 1), file);
    fclose(file);
    if (n < 0) {
        return false;
    }
    buffer[n] = 0;
    if (length != 0) {
        *length = n;
    }
    return true;
}

const char *kOk = "{\"fixture\":\"lab09-session/v1\",\"ok\":true,\"actor\":\"user-a\"}";

const char *kCards =
    "{\"fixture\":\"lab09-session/v1\",\"ok\":true,\"actor\":\"user-a\",\"cards\":["
    "\"c04\",\"c01\",\"c02\",\"c03\",\"c01\",\"c05\",\"c02\",\"c06\","
    "\"c07\",\"c08\",\"c03\",\"c09\",\"c10\",\"c04\",\"c11\",\"c12\"]}";

const char *kExpired = "{\"fixture\":\"lab09-session/v1\",\"session\":\"expired\",\"ok\":false}";

void sign(lab09::Probe *probe, const char *user)
{
    probe->sign_in(user, probe->session().now_ms());
}

bool no_duplicate_cards(const lab09::Probe &probe)
{
    int i;
    int j;
    for (i = 0; i < probe.card_count(); ++i) {
        for (j = i + 1; j < probe.card_count(); ++j) {
            if (strcmp(probe.card_at(i), probe.card_at(j)) == 0) {
                return false;
            }
        }
    }
    return true;
}

int terminal_image(const lab07::SessionWriter &session, uint64_t operation_id)
{
    int i;
    for (i = 0; i < session.event_count(); ++i) {
        if (session.event_at(i).operation_id == operation_id) {
            return session.event_at(i).image_bytes;
        }
    }
    return -1;
}

void test_mute_release()
{
    const char *kinds[] = {"inbox", "media", "playback", "profile"};
    int k;
    lab09::Probe probe;
    unsigned char image[lab09::kImageBytes];
    memset(image, 0x5a, sizeof(image));
    sign(&probe, "user-a");
    probe.arm("I-mute");
    for (k = 0; k < 4; ++k) {
        lab07::ScopedTicket ticket = probe.begin(kinds[k], probe.session().now_ms());
        int stage_before;
        check(ticket.operation_id != 0, "mute begin");
        check(ticket.generation == probe.session().generation(), "generation captured");
        probe.mute(probe.session().now_ms());
        stage_before = probe.session().stage();
        if (strcmp(kinds[k], "media") == 0) {
            check(probe.complete(ticket, 1, 200, (const char *)image, lab09::kImageBytes, probe.session().now_ms()) == 0,
                  "media late body not applied");
        } else if (strcmp(kinds[k], "inbox") == 0) {
            check(probe.complete(ticket, 1, 200, kCards, (int)strlen(kCards), probe.session().now_ms()) == 0,
                  "inbox late body not applied");
        } else {
            check(probe.complete(ticket, 1, 200, kOk, (int)strlen(kOk), probe.session().now_ms()) == 0,
                  "write late body not applied");
        }
        check(probe.session().stage() == stage_before, "late body does not restore stage");
        check(probe.session().stage() == lab07::kStageMuted, "muted");
        check(probe.session().display_power() == 0, "display off");
        check(probe.session().terminal_count_for(ticket.operation_id) == 1, "one mute terminal");
        check(strcmp(probe.session().terminal_for(ticket.operation_id), "stale") == 0 ||
                  strcmp(probe.session().terminal_for(ticket.operation_id), "cancelled") == 0,
              "stale or cancelled");
        check(terminal_image(probe.session(), ticket.operation_id) == 0, "trace image released");
        check(probe.image_bytes() == 0, "image_bytes 0");
        check(probe.card_count() == 0, "cards hidden");
        check(probe.applied_visible() == 0, "applied 0");
        probe.unmute(probe.session().now_ms());
        sign(&probe, "user-a");
    }
}

void test_no_cross_apply()
{
    const char *kinds[] = {"inbox", "media", "playback", "profile"};
    int k;
    unsigned char image[lab09::kImageBytes];
    memset(image, 0x5a, sizeof(image));
    for (k = 0; k < 4; ++k) {
        lab09::Probe probe;
        lab07::ScopedTicket ticket;
        int stage_b;
        uint32_t generation_a;
        uint32_t generation_b;
        sign(&probe, "user-a");
        probe.arm("I-switch");
        ticket = probe.begin(kinds[k], probe.session().now_ms());
        generation_a = ticket.generation;
        sign(&probe, "user-b");
        generation_b = probe.session().generation();
        stage_b = probe.session().stage();
        check(generation_b != generation_a, "B generation differs");
        check(probe.session().terminal_count_for(ticket.operation_id) == 1, "A terminalized on switch");
        if (strcmp(kinds[k], "media") == 0) {
            check(probe.complete(ticket, 1, 200, (const char *)image, lab09::kImageBytes, probe.session().now_ms()) == 0,
                  "A media not applied to B");
        } else if (strcmp(kinds[k], "inbox") == 0) {
            check(probe.complete(ticket, 1, 200, kCards, (int)strlen(kCards), probe.session().now_ms()) == 0,
                  "A inbox not applied to B");
        } else {
            check(probe.complete(ticket, 1, 200, kOk, (int)strlen(kOk), probe.session().now_ms()) == 0,
                  "A write not applied to B");
        }
        check(probe.image_bytes() == 0, "A buffer length 0");
        check(probe.session().stage() == stage_b, "B stage unchanged");
        check(probe.session().generation() == generation_b, "B generation unchanged by A");
        check(strcmp(probe.session().signed_user(), "user-b") == 0, "B remains the actor");
        check(probe.session().terminal_count_for(ticket.operation_id) == 1, "still one terminal");
    }
}

void kind_body(const char *kind, const unsigned char *image, const char **body, int *body_len)
{
    if (strcmp(kind, "media") == 0) {
        *body = (const char *)image;
        *body_len = lab09::kImageBytes;
    } else if (strcmp(kind, "inbox") == 0) {
        *body = kCards;
        *body_len = (int)strlen(kCards);
    } else {
        *body = kOk;
        *body_len = (int)strlen(kOk);
    }
}

void expect_kind_stored(const lab09::Probe &probe, const char *kind, const char *label)
{
    char text[80];
    if (strcmp(kind, "media") == 0) {
        snprintf(text, sizeof(text), "%s image stored", label);
        check(probe.image_bytes() == lab09::kImageBytes, text);
    } else if (strcmp(kind, "inbox") == 0) {
        snprintf(text, sizeof(text), "%s cards stored", label);
        check(probe.card_count() == 12, text);
    } else if (strcmp(kind, "playback") == 0) {
        snprintf(text, sizeof(text), "%s playback stored", label);
        check(strcmp(probe.playback_label(), "playback") == 0, text);
    } else {
        snprintf(text, sizeof(text), "%s profile stored", label);
        check(strcmp(probe.profile_label(), "profile") == 0, text);
    }
}

void expect_kind_cleared(const lab09::Probe &probe, const char *label)
{
    char text[80];
    snprintf(text, sizeof(text), "%s image 0", label);
    check(probe.image_bytes() == 0, text);
    snprintf(text, sizeof(text), "%s no cards", label);
    check(probe.card_count() == 0, text);
    snprintf(text, sizeof(text), "%s playback clear", label);
    check(probe.playback_label()[0] == 0, text);
    snprintf(text, sizeof(text), "%s profile clear", label);
    check(probe.profile_label()[0] == 0, text);
}

void test_historical_redelivery()
{
    const char *kinds[] = {"inbox", "media", "playback", "profile"};
    unsigned char image[lab09::kImageBytes];
    int k;
    memset(image, 0x5a, sizeof(image));
    for (k = 0; k < 4; ++k) {
        lab09::Probe probe;
        lab07::ScopedTicket ticket;
        const char *body = 0;
        int body_len = 0;
        int stage_b;
        uint32_t generation_b;
        char label[64];
        sign(&probe, "user-a");
        ticket = probe.begin(kinds[k], probe.session().now_ms());
        kind_body(kinds[k], image, &body, &body_len);
        snprintf(label, sizeof(label), "%s switch first", kinds[k]);
        check(probe.complete(ticket, 1, 200, body, body_len, probe.session().now_ms()) == 1, label);
        expect_kind_stored(probe, kinds[k], kinds[k]);
        check(probe.session().terminal_count_for(ticket.operation_id) == 1, "switch one success");
        check(strcmp(probe.session().terminal_for(ticket.operation_id), "success") == 0, "switch success");
        sign(&probe, "user-b");
        stage_b = probe.session().stage();
        generation_b = probe.session().generation();
        snprintf(label, sizeof(label), "%s switch redelivery", kinds[k]);
        check(probe.complete(ticket, 1, 200, body, body_len, probe.session().now_ms()) == 0, label);
        expect_kind_cleared(probe, kinds[k]);
        snprintf(label, sizeof(label), "%s B applied 0", kinds[k]);
        check(probe.applied_visible() == 0, label);
        snprintf(label, sizeof(label), "%s B stage", kinds[k]);
        check(probe.session().stage() == stage_b, label);
        snprintf(label, sizeof(label), "%s B generation", kinds[k]);
        check(probe.session().generation() == generation_b, label);
        check(strcmp(probe.active_user(), "user-b") == 0, "actor stays B");
        check(probe.session().terminal_count_for(ticket.operation_id) == 1, "switch still one terminal");
        check(strcmp(probe.session().terminal_for(ticket.operation_id), "success") == 0, "switch success kept");
    }
    for (k = 0; k < 4; ++k) {
        lab09::Probe probe;
        lab07::ScopedTicket ticket;
        const char *body = 0;
        int body_len = 0;
        int stage;
        uint32_t generation;
        char label[64];
        sign(&probe, "user-a");
        ticket = probe.begin(kinds[k], probe.session().now_ms());
        kind_body(kinds[k], image, &body, &body_len);
        snprintf(label, sizeof(label), "%s mute first", kinds[k]);
        check(probe.complete(ticket, 1, 200, body, body_len, probe.session().now_ms()) == 1, label);
        expect_kind_stored(probe, kinds[k], kinds[k]);
        probe.mute(probe.session().now_ms());
        stage = probe.session().stage();
        generation = probe.session().generation();
        check(probe.session().stage() == lab07::kStageMuted, "muted before redelivery");
        check(probe.session().display_power() == 0, "display off before redelivery");
        expect_kind_cleared(probe, "mute cleared");
        snprintf(label, sizeof(label), "%s mute redelivery", kinds[k]);
        check(probe.complete(ticket, 1, 200, body, body_len, probe.session().now_ms()) == 0, label);
        expect_kind_cleared(probe, kinds[k]);
        snprintf(label, sizeof(label), "%s mute stage", kinds[k]);
        check(probe.session().stage() == stage, label);
        check(probe.session().display_power() == 0, "display stays off");
        snprintf(label, sizeof(label), "%s mute generation", kinds[k]);
        check(probe.session().generation() == generation, label);
        check(probe.session().terminal_count_for(ticket.operation_id) == 1, "mute still one terminal");
        check(strcmp(probe.session().terminal_for(ticket.operation_id), "success") == 0, "mute success kept");
    }
}

void test_one_terminal()
{
    lab09::Probe probe;
    lab07::ScopedTicket ticket;
    sign(&probe, "user-a");
    probe.arm("I-push");
    ticket = probe.begin("inbox", probe.session().now_ms());
    check(probe.complete(ticket, 1, 200, kCards, (int)strlen(kCards), probe.session().now_ms()) == 1, "applied once");
    check(probe.complete(ticket, 1, 200, kCards, (int)strlen(kCards), probe.session().now_ms()) == 1, "replay stays applied");
    check(probe.session().terminal_count_for(ticket.operation_id) == 1, "one terminal");
    check(strcmp(probe.session().terminal_for(ticket.operation_id), "success") == 0, "success");
}

void test_signout()
{
    lab09::Probe probe;
    lab07::ScopedTicket ticket;
    sign(&probe, "user-a");
    probe.arm("I-signout");
    ticket = probe.begin("profile", probe.session().now_ms());
    probe.sign_out(probe.session().now_ms());
    check(probe.complete(ticket, 1, 200, kOk, (int)strlen(kOk), probe.session().now_ms()) == 0, "rejected");
    check(probe.session().stage() == lab07::kStageSignedOut, "stays signed out");
    check(probe.session().terminal_count_for(ticket.operation_id) == 1, "one sign-out terminal");
    check(probe.applied_visible() == 0, "not applied");
    check(probe.image_bytes() == 0, "buffer clear");
}

void test_push()
{
    lab09::Probe probe;
    lab07::ScopedTicket ticket;
    int level;
    sign(&probe, "user-a");
    probe.arm("I-push");
    ticket = probe.begin("inbox", probe.session().now_ms());
    check(probe.complete(ticket, 1, 200, kCards, (int)strlen(kCards), probe.session().now_ms()) == 1, "push applied");
    level = probe.session().last_update_level();
    check(level != 2, "cards do not replace the scene");
    check(probe.card_count() == 12, "unique cards");
    check(no_duplicate_cards(probe), "no duplicate card ids");
    check(probe.duplicate_cards() == 0, "duplicate count 0");
    check(probe.session().terminal_count_for(ticket.operation_id) == 1, "one push terminal");
}

void test_expire()
{
    lab09::Probe probe;
    lab07::ScopedTicket ticket;
    int stage;
    sign(&probe, "user-a");
    probe.arm("I-expire-client");
    ticket = probe.begin("media", probe.session().now_ms());
    stage = probe.session().stage();
    check(probe.complete(ticket, 1, 401, kExpired, (int)strlen(kExpired), probe.session().now_ms()) == 0, "not applied");
    check(probe.session().stage() == stage, "stage not restored");
    check(probe.session().terminal_count_for(ticket.operation_id) == 1, "one expire terminal");
    check(strcmp(probe.session().terminal_for(ticket.operation_id), "success") != 0, "not success");
    check(probe.image_bytes() == 0, "expire releases image");
    check(probe.applied_visible() == 0, "applied 0");
}

void test_overflow_and_identity()
{
    lab09::Probe probe;
    lab07::ScopedTicket ticket;
    lab07::ScopedResult blank;
    int i;
    uint64_t active;
    sign(&probe, "user-a");
    probe.arm("I-push");
    ticket = probe.begin("profile", probe.session().now_ms());
    active = ticket.operation_id;
    memset(&blank, 0, sizeof(blank));
    check(probe.session().enqueue_scoped(blank) == true, "unidentified queued");
    probe.session().deliver_scoped_completion(blank, probe.session().now_ms());
    check(probe.session().scoped_operation_id() == active, "active id preserved");
    check(probe.session().terminal_count_for(active) == 0, "unidentified did not terminal the active id");
    for (i = 0; i < lab07::kScopedResultQueueCapacity; ++i) {
        lab07::ScopedResult extra;
        memset(&extra, 0, sizeof(extra));
        extra.operation_id = 1000 + (uint64_t)i;
        extra.generation = 1;
        strncpy(extra.kind, "inbox", sizeof(extra.kind) - 1);
        strncpy(extra.user_id, "user-a", sizeof(extra.user_id) - 1);
        check(probe.session().enqueue_scoped(extra) == true, "queue accepts");
    }
    {
        lab07::ScopedResult overflow;
        memset(&overflow, 0, sizeof(overflow));
        overflow.operation_id = active;
        overflow.generation = ticket.generation;
        strncpy(overflow.kind, "profile", sizeof(overflow.kind) - 1);
        strncpy(overflow.user_id, ticket.user_id, sizeof(overflow.user_id) - 1);
        strncpy(overflow.session_id, ticket.session_id, sizeof(overflow.session_id) - 1);
        overflow.http_status = 200;
        overflow.parse_ok = 1;
        overflow.body_ok = 1;
        check(probe.session().enqueue_scoped(overflow) == false, "overflow rejected");
    }
    check(probe.session().terminal_count_for(active) == 1, "overflow has a terminal");
    check(strcmp(probe.session().terminal_for(active), "failure") == 0, "overflow failure");
    check(probe.session().scoped_queue_count() == lab07::kScopedResultQueueCapacity, "queued results kept");
    check(probe.session().scoped_inflight() == 0, "overflow closed the active op");
    probe.session().deliver_scoped_completion(blank, probe.session().now_ms());
    for (i = 0; i < lab07::kScopedResultQueueCapacity; ++i) {
        check(probe.session().terminal_count_for(1000 + (uint64_t)i) == 1, "queued terminal kept");
    }
}

void test_newer_and_unmute()
{
    lab09::Probe probe;
    lab07::ScopedTicket first;
    lab07::ScopedTicket second;
    int stage;
    sign(&probe, "user-a");
    first = probe.begin("inbox", probe.session().now_ms());
    second = probe.begin("media", probe.session().now_ms());
    check(strcmp(probe.session().terminal_for(first.operation_id), "cancelled") == 0, "newer cancels previous");
    check(probe.session().scoped_operation_id() == second.operation_id, "newer stays active");
    check(probe.complete(first, 1, 200, kCards, (int)strlen(kCards), probe.session().now_ms()) == 0, "old inbox rejected");
    check(probe.session().scoped_operation_id() == second.operation_id, "active preserved");
    check(probe.card_count() == 0, "old cards not visible");
    probe.mute(probe.session().now_ms());
    probe.unmute(probe.session().now_ms());
    stage = probe.session().stage();
    check(probe.complete(second, 1, 200, kOk, (int)strlen(kOk), probe.session().now_ms()) == 0, "unmute does not accept");
    check(probe.session().stage() == stage, "unmute stage stays");
    check(probe.session().terminal_count_for(second.operation_id) == 1, "muted op still one terminal");
}

void test_missing_and_malformed()
{
    lab09::Probe probe;
    lab07::ScopedTicket ticket;
    check(lab09::sign_in_response_ok(0, 0, kOk, (int)strlen(kOk), "user-a") == false, "missing server is not signed in");
    check(lab09::sign_in_response_ok(1, 200, "not-json", 8, "user-a") == false, "malformed sign-in rejected");
    check(lab09::sign_in_response_ok(1, 500, kOk, (int)strlen(kOk), "user-a") == false, "status is not ignored");
    sign(&probe, "user-a");
    ticket = probe.begin("profile", probe.session().now_ms());
    check(probe.complete(ticket, 0, 0, kOk, (int)strlen(kOk), probe.session().now_ms()) == 0, "transport is not applied");
    check(strcmp(probe.session().terminal_for(ticket.operation_id), "success") != 0, "transport is not success");
    ticket = probe.begin("profile", probe.session().now_ms());
    check(probe.complete(ticket, 1, 200, "not-json", 8, probe.session().now_ms()) == 0, "malformed body not applied");
    check(probe.applied_visible() == 0, "still not applied");
}

void test_level()
{
    lab09::Probe probe;
    lab07::ScopedTicket ticket;
    unsigned char image[lab09::kImageBytes];
    memset(image, 0x5a, sizeof(image));
    sign(&probe, "user-a");
    ticket = probe.begin("playback", probe.session().now_ms());
    check(probe.complete(ticket, 1, 200, kOk, (int)strlen(kOk), probe.session().now_ms()) == 1, "playback applied");
    check(probe.session().last_update_level() == 0, "playback is a property update");
    ticket = probe.begin("media", probe.session().now_ms());
    check(probe.complete(ticket, 1, 200, (const char *)image, lab09::kImageBytes, probe.session().now_ms()) == 1,
          "media applied");
    check(probe.image_bytes() == lab09::kImageBytes, "one image stored");
    check(probe.session().last_update_level() == 0, "image does not replace the scene");
    probe.cleanup();
    check(probe.image_bytes() == 0, "cleanup clears image");
}

int read_status(const char *path, int *status)
{
    char buffer[32];
    int length = 0;
    if (!load_file(path, buffer, (int)sizeof(buffer), &length)) {
        return 0;
    }
    *status = atoi(buffer);
    return 1;
}

void print_traces(const lab09::Probe &probe)
{
    char buffer[8192];
    int n = lab09::emit_scoped_traces(probe.session(), buffer, (int)sizeof(buffer));
    if (n > 0) {
        printf("%s\n", buffer);
    }
}

void live_catalog(const char *dir)
{
    const char *kinds[] = {"inbox", "media", "playback", "profile"};
    int k;
    int confused = 0;
    int revoke = 0;
    int leave = 0;
    int expire_status = 0;
    char path[512];
    char body[lab09::kImageBytes + 4];
    char line[256];
    lab09::Probe probe;
    sign(&probe, "user-a");
    probe.arm("I-mute");
    for (k = 0; k < 4; ++k) {
        lab07::ScopedTicket ticket = probe.begin(kinds[k], 0);
        int status = 0;
        int length = 0;
        int perform = 0;
        snprintf(path, sizeof(path), "%s/mute-%s.status", dir, kinds[k]);
        perform = read_status(path, &status);
        snprintf(path, sizeof(path), "%s/mute-%s.body", dir, kinds[k]);
        if (!load_file(path, body, (int)sizeof(body), &length)) {
            perform = 0;
            length = 0;
        }
        probe.mute(0);
        probe.complete(ticket, perform, status, body, length, 0);
        probe.unmute(0);
        sign(&probe, "user-a");
    }
    print_traces(probe);
    {
        lab09::Probe other;
        lab07::ScopedTicket ticket;
        int status = 0;
        int length = 0;
        int perform;
        sign(&other, "user-a");
        other.arm("I-signout");
        ticket = other.begin("profile", 0);
        other.sign_out(0);
        snprintf(path, sizeof(path), "%s/signout.status", dir);
        perform = read_status(path, &status);
        snprintf(path, sizeof(path), "%s/signout.body", dir);
        if (!load_file(path, body, (int)sizeof(body), &length)) {
            perform = 0;
        }
        other.complete(ticket, perform, status, body, length, 0);
        print_traces(other);
    }
    {
        lab09::Probe other;
        lab07::ScopedTicket ticket;
        int status = 0;
        int length = 0;
        int perform;
        sign(&other, "user-a");
        other.arm("I-switch");
        ticket = other.begin("media", 0);
        sign(&other, "user-b");
        snprintf(path, sizeof(path), "%s/switch.status", dir);
        perform = read_status(path, &status);
        snprintf(path, sizeof(path), "%s/switch.body", dir);
        if (!load_file(path, body, (int)sizeof(body), &length)) {
            perform = 0;
        }
        other.complete(ticket, perform, status, body, length, 0);
        print_traces(other);
    }
    {
        lab09::Probe other;
        lab07::ScopedTicket ticket;
        int status = 0;
        int length = 0;
        int perform;
        sign(&other, "user-a");
        other.arm("I-push");
        ticket = other.begin("inbox", 0);
        snprintf(path, sizeof(path), "%s/push.status", dir);
        perform = read_status(path, &status);
        snprintf(path, sizeof(path), "%s/push.body", dir);
        if (!load_file(path, body, (int)sizeof(body), &length)) {
            perform = 0;
        }
        other.complete(ticket, perform, status, body, length, 0);
        print_traces(other);
    }
    {
        lab09::Probe other;
        lab07::ScopedTicket ticket;
        int status = 0;
        int length = 0;
        int perform;
        sign(&other, "user-a");
        other.arm("I-expire-client");
        ticket = other.begin("playback", 0);
        snprintf(path, sizeof(path), "%s/expire.status", dir);
        perform = read_status(path, &status);
        snprintf(path, sizeof(path), "%s/expire.body", dir);
        if (!load_file(path, body, (int)sizeof(body), &length)) {
            perform = 0;
        }
        other.complete(ticket, perform, status, body, length, 0);
        print_traces(other);
    }
    snprintf(path, sizeof(path), "%s/confused.txt", dir);
    if (load_file(path, line, (int)sizeof(line), 0)) {
        confused = atoi(line);
    }
    snprintf(path, sizeof(path), "%s/revoke.txt", dir);
    if (load_file(path, line, (int)sizeof(line), 0)) {
        revoke = atoi(line);
    }
    snprintf(path, sizeof(path), "%s/leave.txt", dir);
    if (load_file(path, line, (int)sizeof(line), 0)) {
        leave = atoi(line);
    }
    snprintf(path, sizeof(path), "%s/expire.status", dir);
    read_status(path, &expire_status);
    {
        char observation[256];
        lab07::format_scoped_observation("I-confused", "none", 0, confused, 0, observation, (int)sizeof(observation));
        printf("%s\n", observation);
        lab07::format_scoped_observation("I-revoke-branches", "revoke-on-mute", 0, 0, revoke, observation,
                                         (int)sizeof(observation));
        printf("%s\n", observation);
        lab07::format_scoped_observation("I-revoke-branches", "leave-server-session", 0, 0, leave, observation,
                                         (int)sizeof(observation));
        printf("%s\n", observation);
        lab07::format_scoped_observation("I-expire-http", "none", expire_status, 0, 0, observation,
                                         (int)sizeof(observation));
        printf("%s\n", observation);
    }
}

} /* namespace */

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "all";
    if (strcmp(name, "mute-release") == 0) {
        test_mute_release();
    } else if (strcmp(name, "no-cross") == 0) {
        test_no_cross_apply();
    } else if (strcmp(name, "one-terminal") == 0) {
        test_one_terminal();
    } else if (strcmp(name, "historical") == 0) {
        test_historical_redelivery();
    } else if (strcmp(name, "signout") == 0) {
        test_signout();
    } else if (strcmp(name, "push") == 0) {
        test_push();
    } else if (strcmp(name, "expire") == 0) {
        test_expire();
    } else if (strcmp(name, "overflow") == 0) {
        test_overflow_and_identity();
    } else if (strcmp(name, "newer") == 0) {
        test_newer_and_unmute();
    } else if (strcmp(name, "missing") == 0) {
        test_missing_and_malformed();
    } else if (strcmp(name, "level") == 0) {
        test_level();
    } else if (strcmp(name, "live-catalog") == 0 && argc > 2) {
        live_catalog(argv[2]);
        return 0;
    } else if (strcmp(name, "sign-in-check") == 0 && argc > 5) {
        char body[2048];
        int length = 0;
        int perform = atoi(argv[2]);
        int status = atoi(argv[3]);
        bool loaded = load_file(argv[4], body, (int)sizeof(body), &length);
        bool accepted = loaded && lab09::sign_in_response_ok(perform, status, body, length, argv[5]);
        printf("accepted=%d\n", accepted ? 1 : 0);
        return 0;
    } else if (strcmp(name, "all") == 0) {
        test_mute_release();
        test_no_cross_apply();
        test_one_terminal();
        test_historical_redelivery();
        test_signout();
        test_push();
        test_expire();
        test_overflow_and_identity();
        test_newer_and_unmute();
        test_missing_and_malformed();
        test_level();
    } else {
        fprintf(stderr, "unknown test %s\n", name);
        return 2;
    }
    if (g_failures != 0) {
        fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    printf("ok %s\n", name);
    return 0;
}
