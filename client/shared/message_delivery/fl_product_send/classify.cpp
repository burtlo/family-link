#include "fl_product_send_classify.hpp"

#include <string.h>

namespace fl_product_send
{
namespace
{

bool body_has_schema(const char *body, int body_len)
{
    if (body == 0 || body_len <= 0) {
        return false;
    }
    return strstr(body, "\"schema\"") != 0;
}

bool body_is_empty_object(const char *body, int body_len)
{
    int i = 0;
    if (body == 0 || body_len < 2) {
        return false;
    }
    while (i < body_len && (body[i] == ' ' || body[i] == '\n')) {
        i += 1;
    }
    return body[i] == '{' && body[i + 1] == '}';
}

} /* namespace */

Com013Class classify_http_result(int wifi_up, int transport_ok, int http_status, const char *body,
                                 int body_len, int refresh_failed)
{
    if (!wifi_up || !transport_ok) {
        return kCom013Queue;
    }
    if (refresh_failed) {
        return kCom013Queue;
    }
    if (http_status == 409) {
        return kCom013Discard;
    }
    if (http_status == 413 || http_status == 415 || http_status == 422) {
        return kCom013Discard;
    }
    if (http_status >= 500 && http_status <= 599) {
        return kCom013Queue;
    }
    if (http_status == 401 || http_status == 403) {
        return kCom013Queue;
    }
    if (http_status == 200) {
        if (body_is_empty_object(body, body_len)) {
            return kCom013Queue;
        }
        if (body_has_schema(body, body_len)) {
            return kCom013Queue;
        }
        return kCom013WaitMalformed;
    }
    if (http_status == 0) {
        return kCom013Queue;
    }
    if (http_status == 400) {
        return kCom013Discard;
    }
    return kCom013Queue;
}

const char *ui_phase_label(SendUiPhase phase)
{
    switch (phase) {
        case kSendUiPreparing:
            return "preparing";
        case kSendUiSending:
            return "sending";
        case kSendUiSent:
            return "sent";
        case kSendUiCouldntSend:
            return "couldn't send";
        case kSendUiQueuedWait:
            return "connecting";
        default:
            return "";
    }
}

const char *connectivity_wait_label(int wifi_up, int refresh_failed)
{
    if (!wifi_up) {
        return "no Wi-Fi";
    }
    if (refresh_failed) {
        return "connecting";
    }
    return "connecting";
}

} /* namespace fl_product_send */
