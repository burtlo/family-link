#include "fl_product_login_session.hpp"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace fl_product_login {
namespace {

void copy_text(char *dst, int cap, const char *src)
{
    int i = 0;
    if (cap <= 0) {
        return;
    }
    if (src == 0) {
        src = "";
    }
    for (; i < cap - 1 && src[i] != 0; ++i) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

bool text_eq(const char *left, const char *right)
{
    if (left == 0 || right == 0) {
        return left == right;
    }
    return strcmp(left, right) == 0;
}

bool pin_digit_ok(char digit)
{
    return digit >= '1' && digit <= '9';
}

bool pin_text_ok(const char *pin)
{
    int i;
    if (pin == 0) {
        return false;
    }
    for (i = 0; i < 4; ++i) {
        if (!pin_digit_ok(pin[i])) {
            return false;
        }
    }
    return pin[4] == 0;
}

uint32_t rotr(uint32_t value, uint32_t bits)
{
    return (value >> bits) | (value << (32 - bits));
}

void sha256_hex(const char *text, char *out, int out_cap)
{
    static const uint32_t kBits[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2
    };
    uint8_t block[64];
    uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint64_t bit_len;
    int length = 0;
    int offset = 0;
    if (text != 0) {
        length = (int)strlen(text);
    }
    bit_len = (uint64_t)length * 8;
    while (offset + 64 <= length) {
        int i;
        memcpy(block, text + offset, 64);
        offset += 64;
        {
            uint32_t w[64];
            uint32_t a;
            uint32_t b;
            uint32_t c;
            uint32_t d;
            uint32_t e;
            uint32_t f;
            uint32_t g;
            uint32_t h;
            for (i = 0; i < 16; ++i) {
                w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
                       ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
            }
            for (i = 16; i < 64; ++i) {
                uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            a = state[0];
            b = state[1];
            c = state[2];
            d = state[3];
            e = state[4];
            f = state[5];
            g = state[6];
            h = state[7];
            for (i = 0; i < 64; ++i) {
                uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
                uint32_t ch = (e & f) ^ ((~e) & g);
                uint32_t temp1 = h + s1 + ch + kBits[i] + w[i];
                uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
                uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                uint32_t temp2 = s0 + maj;
                h = g;
                g = f;
                f = e;
                e = d + temp1;
                d = c;
                c = b;
                b = a;
                a = temp1 + temp2;
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }
    }
    {
        int tail = length - offset;
        int i;
        memset(block, 0, sizeof(block));
        if (tail > 0 && text != 0) {
            memcpy(block, text + offset, (size_t)tail);
        }
        block[tail] = 0x80;
        /* Lab key material is shorter than 56 bytes, so the length fits in this block. */
        block[56] = (uint8_t)((bit_len >> 56) & 0xff);
        block[57] = (uint8_t)((bit_len >> 48) & 0xff);
        block[58] = (uint8_t)((bit_len >> 40) & 0xff);
        block[59] = (uint8_t)((bit_len >> 32) & 0xff);
        block[60] = (uint8_t)((bit_len >> 24) & 0xff);
        block[61] = (uint8_t)((bit_len >> 16) & 0xff);
        block[62] = (uint8_t)((bit_len >> 8) & 0xff);
        block[63] = (uint8_t)(bit_len & 0xff);
        {
            uint32_t w[64];
            uint32_t a;
            uint32_t b;
            uint32_t c;
            uint32_t d;
            uint32_t e;
            uint32_t f;
            uint32_t g;
            uint32_t h;
            for (i = 0; i < 16; ++i) {
                w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
                       ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
            }
            for (i = 16; i < 64; ++i) {
                uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            a = state[0];
            b = state[1];
            c = state[2];
            d = state[3];
            e = state[4];
            f = state[5];
            g = state[6];
            h = state[7];
            for (i = 0; i < 64; ++i) {
                uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
                uint32_t ch = (e & f) ^ ((~e) & g);
                uint32_t temp1 = h + s1 + ch + kBits[i] + w[i];
                uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
                uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                uint32_t temp2 = s0 + maj;
                h = g;
                g = f;
                f = e;
                e = d + temp1;
                d = c;
                c = b;
                b = a;
                a = temp1 + temp2;
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }
    }
    if (out_cap < 65) {
        if (out_cap > 0) {
            out[0] = 0;
        }
        return;
    }
    snprintf(out, (size_t)out_cap,
             "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32,
             state[0], state[1], state[2], state[3], state[4], state[5], state[6], state[7]);
}

} /* namespace */

void SessionWriter::reset_pin()
{
    pin_phase_ = kPinPhaseIdle;
    pin_digit_len_ = 0;
    memset(pin_secret_, 0, sizeof(pin_secret_));
    pin_mask_[0] = 0;
    visible_copy_[0] = 0;
    copy_text(pin_copy_token_, 16, "none");
    selected_user_[0] = 0;
    pin_fingerprint_[0] = 0;
    copy_text(pin_reason_, 16, "none");
    pin_scenario_[0] = 0;
    pin_operation_id_ = 0;
    pin_frozen_generation_ = 0;
    pin_key_generation_ = 0;
    pin_frozen_session_[0] = 0;
    pin_frozen_user_[0] = 0;
    pin_submitted_ = false;
    pin_attempt_ = 0;
    pin_deadline_armed_ = false;
    pin_wait_delay_ = false;
    pin_deadline_ms_ = 0;
    pin_delay_until_ms_ = 0;
    wrong_until_ms_ = 0;
    cooldown_until_ms_ = 0;
    idle_until_ms_ = 0;
    idle_armed_ = false;
    wrong_count_ = 0;
    authenticated_ = 0;
    pin_side_effects_ = 0;
    delay_ms_[0] = 0;
    delay_ms_[1] = 0;
    delay_ms_[2] = 0;
    boot_zero_ = 0;
    boot_partial_ = 0;
    boot_full_ = 0;
    last_update_level_ = 0;
    property_seq_ = 0;
    shown_cooldown_second_ = 0;
    memset(pin_queue_, 0, sizeof(pin_queue_));
    pin_queue_count_ = 0;
    pin_overflow_terminals_ = 0;
    pin_rejected_ = 0;
}

bool SessionWriter::pin_holds_input() const
{
    return pin_submitted_ || pin_digit_len_ > 0 || pin_phase_ == kPinPhaseEntry ||
           pin_phase_ == kPinPhaseChecking || pin_phase_ == kPinPhaseWrong ||
           pin_phase_ == kPinPhaseCooldown;
}

void SessionWriter::refresh_pin_copy()
{
    int i;
    if (pin_phase_ == kPinPhaseChecking) {
        copy_text(visible_copy_, 72, "checking...");
        copy_text(pin_copy_token_, 16, "checking");
    } else if (pin_phase_ == kPinPhaseWrong) {
        copy_text(visible_copy_, 72, "wrong pin");
        copy_text(pin_copy_token_, 16, "wrong_pin");
    } else if (pin_phase_ == kPinPhaseCooldown && cooldown_until_ms_ > now_ms_) {
        int seconds = (int)((cooldown_until_ms_ - now_ms_ + 999) / 1000);
        snprintf(visible_copy_, sizeof(visible_copy_), "Too many attempts. Try again in %d", seconds);
        copy_text(pin_copy_token_, 16, "cooldown");
    } else {
        visible_copy_[0] = 0;
        copy_text(pin_copy_token_, 16, "none");
    }
    for (i = 0; i < pin_digit_len_ && i < 4; ++i) {
        pin_mask_[i] = '*';
    }
    pin_mask_[i] = 0;
}

void SessionWriter::wipe_pin_secret()
{
    memset(pin_secret_, 0, sizeof(pin_secret_));
    pin_digit_len_ = 0;
    pin_mask_[0] = 0;
}

void SessionWriter::mint_pin_key()
{
    char material[96];
    pin_key_generation_ = generation_;
    snprintf(material, sizeof(material), "lab08-key-v1|%s|%llu|%" PRIu32, session_id_,
             (unsigned long long)pin_operation_id_, pin_key_generation_);
    sha256_hex(material, pin_fingerprint_, (int)sizeof(pin_fingerprint_));
    copy_text(pin_frozen_session_, 16, session_id_);
    copy_text(pin_frozen_user_, kMaxId, selected_user_);
    pin_frozen_generation_ = pin_key_generation_;
}

void SessionWriter::release_pin_view()
{
    pin_submitted_ = false;
    pin_deadline_armed_ = false;
    pin_wait_delay_ = false;
    idle_armed_ = false;
}

void SessionWriter::close_pin_operation(const char *terminal, const char *reason)
{
    uint64_t operation_id = pin_operation_id_;
    uint32_t attempt = pin_attempt_ > 0 ? (uint32_t)pin_attempt_ : 1;
    copy_text(pin_reason_, 16, reason);
    release_pin_view();
    pin_operation_id_ = 0;
    if (operation_id != 0) {
        record_terminal(operation_id, attempt, "pin", terminal);
    }
}

void SessionWriter::schedule_pin_retry()
{
    static const int kDelays[3] = {kPinRetryDelay0Ms, kPinRetryDelay1Ms, kPinRetryDelay2Ms};
    int index = pin_attempt_ - 1;
    if (index < 0 || index > 2) {
        return;
    }
    delay_ms_[index] = kDelays[index];
    pin_wait_delay_ = true;
    pin_deadline_armed_ = false;
    pin_delay_until_ms_ = now_ms_ + kDelays[index];
    pin_phase_ = kPinPhaseChecking;
    refresh_pin_copy();
    last_update_level_ = 0;
    property_seq_ += 1;
}

void SessionWriter::fail_pin_transport()
{
    if (pin_attempt_ < kPinMaxAttempts) {
        schedule_pin_retry();
        return;
    }
    authenticated_ = 0;
    wipe_pin_secret();
    pin_phase_ = kPinPhaseIdle;
    refresh_pin_copy();
    if (stage_ != kStageMuted && stage_ != kStageWifiUnavailable) {
        apply_stage(kStageConnecting, "pin_transport_exhausted");
        selectable_cards_ = 0;
    }
    close_pin_operation("timed_out", "transport");
}

void SessionWriter::start_pin_submission(int64_t now_ms)
{
    now_ms_ = now_ms;
    if (pin_operation_id_ == 0) {
        pin_operation_id_ = next_operation_id_;
        next_operation_id_ += 1;
    }
    pin_phase_ = kPinPhaseChecking;
    pin_submitted_ = true;
    pin_attempt_ = 1;
    delay_ms_[0] = 0;
    delay_ms_[1] = 0;
    delay_ms_[2] = 0;
    pin_wait_delay_ = false;
    pin_deadline_armed_ = true;
    pin_deadline_ms_ = now_ms + kPinAttemptTimeoutMs;
    idle_armed_ = false;
    authenticated_ = 0;
    mint_pin_key();
    refresh_pin_copy();
    last_update_level_ = 0;
    property_seq_ += 1;
}

void SessionWriter::accept_pin_result(const PinResult &result)
{
    if (result.operation_id == 0) {
        return;
    }
    if (terminal_count_for(result.operation_id) > 0) {
        return;
    }
    if (!pin_submitted_ || result.operation_id != pin_operation_id_) {
        copy_text(pin_reason_, 16, "stale");
        record_terminal(result.operation_id, 1, "pin", "stale");
        return;
    }
    if (stage_ == kStageMuted) {
        wipe_pin_secret();
        pin_phase_ = kPinPhaseIdle;
        refresh_pin_copy();
        close_pin_operation("stale", "mute");
        return;
    }
    if ((result.generation != 0 && result.generation != pin_key_generation_) ||
        (result.user_id[0] != 0 && !text_eq(result.user_id, pin_frozen_user_)) ||
        (result.session_id[0] != 0 && !text_eq(result.session_id, pin_frozen_session_))) {
        wipe_pin_secret();
        pin_phase_ = kPinPhaseIdle;
        refresh_pin_copy();
        close_pin_operation("stale", "stale");
        return;
    }
    if (result.side_effects_valid) {
        pin_side_effects_ = result.side_effects;
    }
    if (result.outcome == kPinOutcomeSuccess && result.parse_ok == 1 && result.http_status == 200 &&
        stage_ != kStageMuted) {
        authenticated_ = 1;
        wrong_count_ = 0;
        cooldown_until_ms_ = 0;
        shown_cooldown_second_ = 0;
        wipe_pin_secret();
        pin_phase_ = kPinPhaseIdle;
        refresh_pin_copy();
        last_update_level_ = 0;
        property_seq_ += 1;
        close_pin_operation("success", "ok");
        return;
    }
    if (result.outcome == kPinOutcomeSuccess) {
        fail_pin_transport();
        return;
    }
    if (result.outcome == kPinOutcomeWrong) {
        authenticated_ = 0;
        wrong_count_ += 1;
        wipe_pin_secret();
        release_pin_view();
        if (wrong_count_ >= kPinWrongLimit) {
            pin_phase_ = kPinPhaseCooldown;
            cooldown_until_ms_ = now_ms_ + kPinCooldownMs;
            shown_cooldown_second_ = (int)((kPinCooldownMs + 999) / 1000);
            refresh_pin_copy();
        } else {
            pin_phase_ = kPinPhaseWrong;
            wrong_until_ms_ = now_ms_ + kWrongPinCopyMs;
            refresh_pin_copy();
        }
        last_update_level_ = 0;
        property_seq_ += 1;
        close_pin_operation("failure", "credential");
        return;
    }
    if (result.outcome == kPinOutcomeUnknownUser) {
        authenticated_ = 0;
        wipe_pin_secret();
        pin_phase_ = kPinPhaseIdle;
        refresh_pin_copy();
        last_update_level_ = 0;
        property_seq_ += 1;
        close_pin_operation("failure", "unknown_user");
        return;
    }
    fail_pin_transport();
}

void SessionWriter::drain_pin()
{
    int i;
    int count = pin_queue_count_;
    PinResult pending[kPinResultQueueCapacity];
    for (i = 0; i < count; ++i) {
        pending[i] = pin_queue_[i];
    }
    pin_queue_count_ = 0;
    for (i = 0; i < count; ++i) {
        accept_pin_result(pending[i]);
    }
}

bool SessionWriter::enqueue_pin(const PinResult &result)
{
    if (pin_queue_count_ >= kPinResultQueueCapacity) {
        pin_rejected_ += 1;
        if (result.operation_id != 0 && terminal_count_for(result.operation_id) == 0) {
            copy_text(pin_reason_, 16, "overflow");
            if (pin_submitted_ && result.operation_id == pin_operation_id_) {
                wipe_pin_secret();
                pin_phase_ = kPinPhaseIdle;
                refresh_pin_copy();
                release_pin_view();
                pin_operation_id_ = 0;
            }
            if (record_terminal(result.operation_id, 1, "pin", "failure")) {
                pin_overflow_terminals_ += 1;
            }
        }
        return false;
    }
    pin_queue_[pin_queue_count_] = result;
    pin_queue_count_ += 1;
    return true;
}

void SessionWriter::deliver_pin_completion(const PinResult &result, int64_t now_ms)
{
    now_ms_ = now_ms;
    if (!enqueue_pin(result)) {
        return;
    }
    drain_pin();
}

void SessionWriter::cancel_pin(int64_t now_ms)
{
    uint64_t operation_id;
    uint32_t attempt;
    now_ms_ = now_ms;
    if (pin_operation_id_ == 0 || terminal_count_for(pin_operation_id_) > 0) {
        wipe_pin_secret();
        pin_phase_ = kPinPhaseIdle;
        refresh_pin_copy();
        release_pin_view();
        pin_operation_id_ = 0;
        return;
    }
    operation_id = pin_operation_id_;
    attempt = pin_attempt_ > 0 ? (uint32_t)pin_attempt_ : 1;
    wipe_pin_secret();
    pin_phase_ = kPinPhaseIdle;
    refresh_pin_copy();
    release_pin_view();
    pin_operation_id_ = 0;
    copy_text(pin_reason_, 16, "cancelled");
    last_update_level_ = 0;
    property_seq_ += 1;
    record_terminal(operation_id, attempt, "pin", "cancelled");
}

bool SessionWriter::take_pin_for_mute(uint64_t *operation_id, uint32_t *attempt)
{
    wipe_pin_secret();
    pin_phase_ = kPinPhaseIdle;
    refresh_pin_copy();
    release_pin_view();
    if (pin_operation_id_ == 0 || terminal_count_for(pin_operation_id_) > 0) {
        pin_operation_id_ = 0;
        return false;
    }
    *operation_id = pin_operation_id_;
    *attempt = pin_attempt_ > 0 ? (uint32_t)pin_attempt_ : 1;
    pin_operation_id_ = 0;
    return true;
}

void SessionWriter::select_pin_user(const char *user_id, int64_t now_ms)
{
    now_ms_ = now_ms;
    if (user_id == 0 || user_id[0] == 0 || stage_ == kStageMuted) {
        return;
    }
    if (text_eq(selected_user_, user_id)) {
        if (!pin_submitted_ && pin_phase_ == kPinPhaseIdle && stage_ == kStageRoster) {
            pin_phase_ = kPinPhaseEntry;
            refresh_pin_copy();
            last_update_level_ = 0;
            property_seq_ += 1;
        }
        return;
    }
    if (pin_operation_id_ != 0 && terminal_count_for(pin_operation_id_) == 0) {
        cancel_pin(now_ms);
    }
    wrong_count_ = 0;
    cooldown_until_ms_ = 0;
    shown_cooldown_second_ = 0;
    wipe_pin_secret();
    copy_text(selected_user_, kMaxId, user_id);
    copy_text(focus_id_, kMaxId, user_id);
    pin_phase_ = kPinPhaseEntry;
    refresh_pin_copy();
    last_update_level_ = 0;
    property_seq_ += 1;
}

bool SessionWriter::enter_pin_digit(char digit, int64_t now_ms)
{
    now_ms_ = now_ms;
    if (!keypad_open() || !pin_digit_ok(digit) || selected_user_[0] == 0) {
        return false;
    }
    if (cooldown_until_ms_ > now_ms_) {
        pin_phase_ = kPinPhaseCooldown;
        refresh_pin_copy();
        return false;
    }
    if (pin_phase_ != kPinPhaseIdle && pin_phase_ != kPinPhaseEntry) {
        return false;
    }
    if (pin_digit_len_ >= 4) {
        return false;
    }
    if (pin_digit_len_ == 0 && pin_operation_id_ == 0) {
        pin_operation_id_ = next_operation_id_;
        next_operation_id_ += 1;
        pin_frozen_generation_ = generation_;
        copy_text(pin_frozen_session_, 16, session_id_);
        copy_text(pin_frozen_user_, kMaxId, selected_user_);
    }
    pin_secret_[pin_digit_len_] = digit;
    pin_digit_len_ += 1;
    pin_secret_[pin_digit_len_] = 0;
    pin_phase_ = kPinPhaseEntry;
    idle_until_ms_ = now_ms + kPartialPinIdleMs;
    idle_armed_ = pin_digit_len_ < 4;
    refresh_pin_copy();
    last_update_level_ = 0;
    property_seq_ += 1;
    if (pin_digit_len_ == 4) {
        idle_armed_ = false;
        start_pin_submission(now_ms);
    }
    return true;
}

void SessionWriter::press_boot(int64_t now_ms)
{
    now_ms_ = now_ms;
    if (stage_ == kStageMuted) {
        return;
    }
    if (pin_phase_ == kPinPhaseChecking || pin_submitted_) {
        boot_full_ = 1;
        last_update_level_ = 0;
        return;
    }
    if (pin_digit_len_ >= 1 && pin_digit_len_ <= 3) {
        boot_partial_ = 1;
        wipe_pin_secret();
        idle_armed_ = false;
        pin_phase_ = kPinPhaseEntry;
        refresh_pin_copy();
        last_update_level_ = 0;
        property_seq_ += 1;
        return;
    }
    boot_zero_ = 1;
    pin_phase_ = kPinPhaseIdle;
    refresh_pin_copy();
    last_update_level_ = 0;
    property_seq_ += 1;
}

uint64_t SessionWriter::begin_pin_request(const char *user_id, const char *pin, int64_t now_ms)
{
    int i;
    now_ms_ = now_ms;
    if (stage_ == kStageMuted || user_id == 0 || user_id[0] == 0 || !pin_text_ok(pin)) {
        return 0;
    }
    if (pin_submitted_ && pin_operation_id_ != 0) {
        cancel_pin(now_ms);
    } else if (pin_operation_id_ != 0 && !text_eq(selected_user_, user_id)) {
        cancel_pin(now_ms);
    }
    if (!text_eq(selected_user_, user_id)) {
        wrong_count_ = 0;
        cooldown_until_ms_ = 0;
        shown_cooldown_second_ = 0;
        copy_text(selected_user_, kMaxId, user_id);
        copy_text(focus_id_, kMaxId, user_id);
    }
    wipe_pin_secret();
    for (i = 0; i < 4; ++i) {
        pin_secret_[i] = pin[i];
    }
    pin_secret_[4] = 0;
    pin_digit_len_ = 4;
    if (pin_operation_id_ == 0) {
        pin_operation_id_ = next_operation_id_;
        next_operation_id_ += 1;
    }
    start_pin_submission(now_ms);
    return pin_operation_id_;
}

void SessionWriter::arm_pin_scenario(const char *scenario)
{
    copy_text(pin_scenario_, 32, scenario);
}

void SessionWriter::note_remote_observation(int side_effects, const char *reason)
{
    pin_side_effects_ = side_effects;
    copy_text(pin_reason_, 16, reason == 0 ? "none" : reason);
}

bool SessionWriter::copy_pin_request(char *user, int user_cap, char *pin, int pin_cap, char *key,
                                    int key_cap) const
{
    if (!pin_submitted_) {
        return false;
    }
    copy_text(user, user_cap, pin_frozen_user_);
    copy_text(pin, pin_cap, pin_secret_);
    copy_text(key, key_cap, pin_fingerprint_);
    return pin_secret_[0] != 0 && pin_fingerprint_[0] != 0;
}

int64_t SessionWriter::pin_next_event_ms() const
{
    const int64_t kFar = 0x7fffffffffffffffLL;
    int64_t next = kFar;
    if (pin_phase_ == kPinPhaseChecking && pin_submitted_ && pin_wait_delay_) {
        next = pin_delay_until_ms_;
    } else if (pin_phase_ == kPinPhaseChecking && pin_submitted_ && pin_deadline_armed_) {
        next = pin_deadline_ms_;
    }
    if (pin_phase_ == kPinPhaseWrong && wrong_until_ms_ > 0 && wrong_until_ms_ < next) {
        next = wrong_until_ms_;
    }
    if (cooldown_until_ms_ > now_ms_ && cooldown_until_ms_ < next) {
        next = cooldown_until_ms_;
    }
    if (idle_armed_ && pin_phase_ == kPinPhaseEntry && pin_digit_len_ > 0 && pin_digit_len_ < 4 &&
        idle_until_ms_ < next) {
        next = idle_until_ms_;
    }
    if (next <= now_ms_) {
        return now_ms_;
    }
    return next;
}

void SessionWriter::on_pin_clock()
{
    if (pin_phase_ == kPinPhaseChecking && pin_submitted_ && pin_wait_delay_ &&
        now_ms_ >= pin_delay_until_ms_) {
        pin_wait_delay_ = false;
        pin_attempt_ += 1;
        pin_deadline_ms_ = now_ms_ + kPinAttemptTimeoutMs;
        pin_deadline_armed_ = true;
        last_update_level_ = 0;
        property_seq_ += 1;
        return;
    }
    if (pin_phase_ == kPinPhaseChecking && pin_submitted_ && pin_deadline_armed_ && !pin_wait_delay_ &&
        now_ms_ >= pin_deadline_ms_) {
        PinResult result;
        pin_deadline_armed_ = false;
        memset(&result, 0, sizeof(result));
        result.operation_id = pin_operation_id_;
        result.generation = pin_key_generation_;
        copy_text(result.session_id, 16, pin_frozen_session_);
        copy_text(result.user_id, kMaxId, pin_frozen_user_);
        result.outcome = kPinOutcomeTimeout;
        accept_pin_result(result);
        return;
    }
    if (pin_phase_ == kPinPhaseWrong && wrong_until_ms_ > 0 && now_ms_ >= wrong_until_ms_) {
        wrong_until_ms_ = 0;
        pin_phase_ = kPinPhaseEntry;
        refresh_pin_copy();
        last_update_level_ = 0;
        property_seq_ += 1;
        return;
    }
    if (cooldown_until_ms_ > 0 && now_ms_ >= cooldown_until_ms_) {
        cooldown_until_ms_ = 0;
        wrong_count_ = 0;
        shown_cooldown_second_ = 0;
        if (pin_phase_ == kPinPhaseCooldown) {
            pin_phase_ = kPinPhaseEntry;
        }
        refresh_pin_copy();
        last_update_level_ = 0;
        property_seq_ += 1;
        return;
    }
    if (idle_armed_ && pin_phase_ == kPinPhaseEntry && pin_digit_len_ > 0 && pin_digit_len_ < 4 &&
        now_ms_ >= idle_until_ms_) {
        uint64_t operation_id = pin_operation_id_;
        idle_armed_ = false;
        wipe_pin_secret();
        pin_phase_ = kPinPhaseIdle;
        refresh_pin_copy();
        release_pin_view();
        pin_operation_id_ = 0;
        copy_text(pin_reason_, 16, "idle");
        last_update_level_ = 0;
        property_seq_ += 1;
        if (operation_id != 0) {
            record_terminal(operation_id, 1, "pin", "cancelled");
        }
    }
}

void SessionWriter::note_pin_presented_time()
{
    int second;
    if (pin_phase_ != kPinPhaseCooldown || cooldown_until_ms_ <= now_ms_) {
        return;
    }
    second = (int)((cooldown_until_ms_ - now_ms_ + 999) / 1000);
    if (second == shown_cooldown_second_) {
        refresh_pin_copy();
        return;
    }
    shown_cooldown_second_ = second;
    refresh_pin_copy();
    last_update_level_ = 0;
    property_seq_ += 1;
}

int SessionWriter::pin_phase() const { return pin_phase_; }
int SessionWriter::digit_len() const { return pin_digit_len_; }
int SessionWriter::display_power() const { return stage_ == kStageMuted ? 0 : 1; }
const char *SessionWriter::pin_mask() const { return pin_mask_; }
const char *SessionWriter::visible_copy() const { return visible_copy_; }
const char *SessionWriter::pin_copy_token() const { return pin_copy_token_; }
int SessionWriter::wrong_count() const { return wrong_count_; }
int SessionWriter::cooldown_remaining_ms() const
{
    if (cooldown_until_ms_ <= now_ms_) {
        return 0;
    }
    return (int)(cooldown_until_ms_ - now_ms_);
}
bool SessionWriter::keypad_open() const
{
    if (stage_ == kStageMuted || pin_submitted_ || pin_phase_ == kPinPhaseChecking ||
        pin_phase_ == kPinPhaseWrong || pin_phase_ == kPinPhaseCooldown) {
        return false;
    }
    return true;
}
bool SessionWriter::pin_submitted() const { return pin_submitted_; }
uint64_t SessionWriter::pin_operation_id() const { return pin_operation_id_; }
uint32_t SessionWriter::pin_key_generation() const { return pin_key_generation_; }
const char *SessionWriter::pin_key_fingerprint() const { return pin_fingerprint_; }
int SessionWriter::pin_attempt() const { return pin_attempt_; }
int SessionWriter::pin_delay_ms(int index) const
{
    if (index < 0 || index > 2) {
        return 0;
    }
    return delay_ms_[index];
}
int SessionWriter::authenticated() const { return authenticated_; }
int SessionWriter::last_update_level() const { return last_update_level_; }
uint32_t SessionWriter::property_seq() const { return property_seq_; }
int SessionWriter::pin_queue_count() const { return pin_queue_count_; }
int SessionWriter::pin_overflow_terminals() const { return pin_overflow_terminals_; }
int SessionWriter::boot_zero() const { return boot_zero_; }
int SessionWriter::boot_partial() const { return boot_partial_; }
int SessionWriter::boot_full() const { return boot_full_; }

int format_pin_trace_line(const TerminalRecord &record, char *buffer, int capacity)
{
    return snprintf(
        buffer, (size_t)capacity,
        "LAB08_TRACE schema=lab08-trace/v1 event=scenario_terminal seq=%" PRIu32 " monotonic_us=%lld "
        "session_id=%s user_id=%s operation_id=%llu generation=%" PRIu32 " key_generation=%" PRIu32 " "
        "scenario=%s class=%s terminal=%s terminal_count=%d attempt=%" PRIu32 " stage=%s "
        "digit_len=%d display_power=%d wrong_count=%d cooldown_remaining_ms=%d side_effects=%d "
        "delay_ms_0=%d delay_ms_1=%d delay_ms_2=%d attempts_used=%d boot_zero=%d boot_partial=%d "
        "boot_full=%d pin_copy=%s reason=%s key_fingerprint=%s update_level=%d",
        record.generation, (long long)record.monotonic_us, record.session_id, record.user_id,
        (unsigned long long)record.operation_id, record.generation, record.key_generation,
        record.scenario, record.cls, record.terminal, record.terminal_count, record.attempt,
        record.stage, record.digit_len, record.display_power, record.wrong_count,
        record.cooldown_remaining_ms, record.side_effects, record.delay_ms_0, record.delay_ms_1,
        record.delay_ms_2, record.attempts_used, record.boot_zero, record.boot_partial, record.boot_full,
        record.pin_copy[0] ? record.pin_copy : "none", record.reason[0] ? record.reason : "none",
        record.key_fingerprint[0] ? record.key_fingerprint : "none", record.update_level);
}

} /* namespace fl_product_login */
