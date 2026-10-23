#ifndef FL_PRODUCT_LOGIN_SESSION_HPP
#define FL_PRODUCT_LOGIN_SESSION_HPP

#include "fl_product_login_stages.hpp"

#include <stdint.h>

namespace fl_product_login {

enum {
    kRosterResultQueueCapacity = 4,
    kPinResultQueueCapacity = 2,
    kScopedResultQueueCapacity = 8,
    kActiveRosterPeriodMs = 60000,
    kIdleQuietMs = 600000,
    kIdleRosterPeriodMs = 300000,
    kHeartbeatPeriodMs = 60000,
    kSessionRefreshPeriodMs = 60000,
    kSessionCredentialBoundMs = 180000,
    kInvalidateRetryDelay0Ms = 0,
    kInvalidateRetryDelay1Ms = 1000,
    kInvalidateRetryDelay2Ms = 2000,
    kSessionTokenCap = 48,
    kRequestTimeoutMs = 1000,
    kConnectingRequestTimeoutMs = 3000,
    kPinAttemptTimeoutMs = 1000,
    kPinMaxAttempts = 4,
    kPinRetryDelay0Ms = 1000,
    kPinRetryDelay1Ms = 2000,
    kPinRetryDelay2Ms = 4000,
    kWrongPinCopyMs = 2000,
    kPinCooldownMs = 60000,
    kPartialPinIdleMs = 60000,
    kPinWrongLimit = 5,
    kStaleAfterMissedBeats = 3,
    kMaxUsers = 8,
    kMaxId = 16,
    kMaxEvents = 96
};

enum PinPhase {
    kPinPhaseIdle = 0,
    kPinPhaseEntry = 1,
    kPinPhaseChecking = 2,
    kPinPhaseWrong = 3,
    kPinPhaseCooldown = 4
};

enum PinOutcome {
    kPinOutcomeNone = 0,
    kPinOutcomeTransport = 1,
    kPinOutcomeSuccess = 2,
    kPinOutcomeWrong = 3,
    kPinOutcomeTimeout = 4,
    kPinOutcomeUnknownUser = 5
};

struct UserId {
    char id[kMaxId];
    char display_name[32];
    char avatar_id[24];
    char card_background_id[24];
};

struct RosterResult {
    uint64_t operation_id;
    int http_status;
    int valid;
    int parse_ok;
    int user_count;
    UserId users[kMaxUsers];
};

/* Completion identity is captured at submission. operation_id 0 stays 0. */
struct PinResult {
    uint64_t operation_id;
    uint32_t generation;
    char session_id[16];
    char user_id[kMaxId];
    int http_status;
    int parse_ok;
    int outcome;
    int side_effects;
    int side_effects_valid;
    char session_token[kSessionTokenCap];
    int session_token_valid;
};

struct RefreshResult {
    uint64_t operation_id;
    int http_status;
    int perform_ok;
    int parse_ok;
};

struct InvalidateTicket {
    int active;
    uint64_t pin_operation_id;
    char operation_id[40];
    char session_token[kSessionTokenCap];
    int attempt;
};

struct TerminalRecord {
    uint64_t operation_id;
    uint32_t attempt;
    char kind[16];
    char terminal[16];
    int terminal_count;
    char scenario[32];
    char cls[16];
    char stage[24];
    int user_count;
    int selectable_cards;
    int stale;
    int missed_beats;
    int inflight_requests;
    int touch_during_wait;
    int focus_stable;
    int stage_unchanged;
    int valid;
    int roster_period_ms;
    int heartbeat_period_ms;
    char focus_id[kMaxId];
    char user_id[kMaxId];
    char session_id[16];
    int64_t monotonic_us;
    uint32_t generation;
    int queue_count;
    char parsed_keys[80];
    int grace_ms;
    int clear_stale;
    int digit_len;
    int display_power;
    int wrong_count;
    int cooldown_remaining_ms;
    int side_effects;
    int delay_ms_0;
    int delay_ms_1;
    int delay_ms_2;
    int attempts_used;
    int boot_zero;
    int boot_partial;
    int boot_full;
    int update_level;
    uint32_t key_generation;
    char pin_copy[16];
    char reason[16];
    char key_fingerprint[68];
    uint32_t session_generation;
    int applied;
    int image_bytes;
    int card_count;
    int duplicate_cards;
    int http_status;
    int b_side_effects;
    int stage_restored;
};

/* Identity is captured at submission. operation_id 0 stays 0.
   A completion must not be filled from the current user, session, or next id. */
struct ScopedTicket {
    uint64_t operation_id;
    uint32_t generation;
    char user_id[kMaxId];
    char session_id[16];
    char kind[16];
};

struct ScopedResult {
    uint64_t operation_id;
    uint32_t generation;
    char user_id[kMaxId];
    char session_id[16];
    char kind[16];
    int http_status;
    int parse_ok;
    int body_ok;
    int expired;
};

bool scoped_kind_name(const char *kind);

/* Reusable owner surface: roster begin/deliver plus the PIN begin/deliver/cancel/read
   surface LAB-08 and LAB-13 call. Fixture URLs, HTTP, catalogs, and on-screen controls
   stay outside this header. One writer applies stage. */

class SessionWriter {
public:
    SessionWriter();

    void boot(int64_t now_ms, bool wifi_up);
    void set_wifi(bool up, int64_t now_ms);
    void note_user_activity(int64_t now_ms);
    void mute(int64_t now_ms);
    void unmute(int64_t now_ms);
    void begin_hold_probe(int64_t now_ms);
    void request_roster(int64_t now_ms);
    /* Starts one roster request and returns the id captured at submission.
       Returns 0 when no new request starts. Does not mint an id for a later result. */
    uint64_t begin_roster_request(int64_t now_ms);
    void note_touch_during_wait();
    void note_transport_failure();
    void ack_health();
    /* Stores the result unchanged. Does not copy the in-flight id or
       next_operation_id_ onto operation id 0. */
    bool enqueue_roster(const RosterResult &result);
    void drain();
    void submit_roster(const RosterResult &result, int64_t now_ms);
    /* Deliver a completion that already carries the id from submission.
       Does not read the current in-flight id or next_operation_id_ to fill a missing id. */
    void deliver_roster_completion(const RosterResult &result, int64_t now_ms);
    /* PIN surface. begin returns the id captured at submission, or 0 if none starts.
       deliver does not copy the current in-flight id onto operation id 0.
       cancel is local and does not roll back a remote side effect. */
    void select_pin_user(const char *user_id, int64_t now_ms);
    bool enter_pin_digit(char digit, int64_t now_ms);
    void press_boot(int64_t now_ms);
    uint64_t begin_pin_request(const char *user_id, const char *pin, int64_t now_ms);
    bool enqueue_pin(const PinResult &result);
    void deliver_pin_completion(const PinResult &result, int64_t now_ms);
    void cancel_pin(int64_t now_ms);
    /* Generation-scoped surface LAB-09 and LAB-13 call. One in-flight operation.
       begin snapshots operation id, session generation, user, and session id.
       deliver returns 1 only when this call newly applies that captured user and
       generation. A historical success is not a new apply.
       deliver does not copy the current user or next id onto a zero id.
       cancel is local and does not roll back a remote side effect. */
    void sign_in(const char *user_id, int64_t now_ms);
    void sign_out(int64_t now_ms);
    ScopedTicket begin_scoped_operation(const char *kind, int64_t now_ms);
    bool enqueue_scoped(const ScopedResult &result);
    int deliver_scoped_completion(const ScopedResult &result, int64_t now_ms);
    void cancel_scoped(int64_t now_ms);
    void arm_scoped_scenario(const char *scenario);
    void note_scoped_payload(int image_bytes, int card_count, int duplicate_cards, int http_status);
    void note_scoped_observation(int b_side_effects);
    void arm_pin_scenario(const char *scenario);
    void note_remote_observation(int side_effects, const char *reason);
    /* Signed-in credential (P5). Issued on PIN success; cleared on mute. */
    uint64_t begin_refresh_request(int64_t now_ms);
    void deliver_refresh_completion(const RefreshResult &result, int64_t now_ms);
    InvalidateTicket take_invalidate_job(int64_t now_ms);
    void deliver_invalidate_completion(uint64_t operation_id, int attempt, int http_status, int perform_ok,
                                       int64_t now_ms);
    void deliver_health_completion(int perform_ok, int http_status, const char *body, int body_len, int64_t now_ms);
    int credential_active() const;
    int credential_expired() const;
    const char *credential_token() const;
    int64_t credential_bound_ms() const;
    int64_t next_refresh_due_ms() const;
    int refresh_inflight() const;
    uint64_t refresh_operation_id() const;
    int invalidate_attempts() const;
    /* Copies the frozen request for the worker. Returns false when none is submitted.
       Callers must not write the PIN into a trace or log. */
    bool copy_pin_request(char *user, int user_cap, char *pin, int pin_cap, char *key, int key_cap) const;
    void advance_to(int64_t now_ms);
    void arm_scenario(const char *scenario);
    void publish(const char *scenario, const char *terminal);
    void annotate_last(const char *parsed_keys, int grace_ms, int clear_stale);

    int stage() const;
    const char *stage_name() const;
    int user_count() const;
    const char *focus_id() const;
    const char *user_at(int index) const;
    const char *user_display_name_at(int index) const;
    const char *user_card_background_at(int index) const;
    int user_uses_initials_fallback_at(int index) const;
    const char *user_avatar_id_at(int index) const;
    int selectable_cards() const;
    int stale() const;
    int missed_beats() const;
    int inflight_requests() const;
    int touch_during_wait() const;
    int queue_count() const;
    int overflow_terminals() const;
    int rejected_results() const;
    bool roster_inflight() const;
    uint32_t roster_attempt() const;
    uint64_t roster_operation_id() const;
    int64_t now_ms() const;
    int64_t next_roster_due_ms() const;
    int64_t next_heartbeat_due_ms() const;
    int roster_period_ms() const;
    int heartbeat_period_ms() const;
    bool has_wrong_pin() const;
    int pin_phase() const;
    int digit_len() const;
    int display_power() const;
    const char *pin_mask() const;
    const char *visible_copy() const;
    const char *pin_copy_token() const;
    int wrong_count() const;
    int cooldown_remaining_ms() const;
    bool keypad_open() const;
    bool pin_submitted() const;
    uint64_t pin_operation_id() const;
    uint32_t pin_key_generation() const;
    const char *pin_key_fingerprint() const;
    int pin_attempt() const;
    int pin_delay_ms(int index) const;
    int authenticated() const;
    int last_update_level() const;
    uint32_t property_seq() const;
    int pin_queue_count() const;
    int pin_overflow_terminals() const;
    int boot_zero() const;
    int boot_partial() const;
    int boot_full() const;
    int terminal_count_for(uint64_t operation_id) const;
    const char *terminal_for(uint64_t operation_id) const;
    int event_count() const;
    const TerminalRecord &event_at(int index) const;
    uint32_t generation() const;
    const char *session_id() const;
    int signed_in() const;
    const char *signed_user() const;
    int scoped_applied() const;
    int scoped_inflight() const;
    uint64_t scoped_operation_id() const;
    uint32_t scoped_generation() const;
    int scoped_queue_count() const;
    int scoped_overflow_terminals() const;
    int connecting_backoff_index() const;
    bool health_ack_pending() const;
    uint64_t health_operation_id() const;
    int health_ack_count() const;
    const char *health_server_time() const;
    const char *health_reason() const;
    const char *boot_id() const;
    /* True only when a PIN POST may leave now. False during the 1/2/4 s retry wait. */
    bool pin_transport_ready() const;
    /* Connection status occupies the PIN lower slot. Keypad, BOOT, and the idle timer wait. */
    bool pin_lower_is_status() const;
    int roster_request_timeout_ms() const;
    int roster_updating() const;
    int hangout_known() const;

private:
    void reset_runtime();
    void apply_stage(int stage, const char *trigger);
    void begin_roster(int64_t now_ms, bool periodic, bool hold);
    void begin_health(int64_t now_ms, const char *trigger);
    void clear_roster_inflight();
    void on_roster_timeout();
    void on_heartbeat_due();
    void on_roster_due();
    void apply_roster_body(RosterResult &result);
    bool same_membership(const RosterResult &result) const;
    void preserve_focus(const RosterResult &result);
    bool record_terminal(uint64_t operation_id, uint32_t attempt, const char *kind,
                         const char *terminal);
    void fill_snapshot(TerminalRecord *record) const;
    void remember_users(const RosterResult &result);
    int64_t quiet_anchor_ms() const;
    void pull_roster_period_forward(int64_t now_ms);
    void reset_pin();
    void apply_roster_while_pin(RosterResult &result);
    bool pin_holds_input() const;
    int64_t pin_next_event_ms() const;
    void on_pin_clock();
    void note_pin_presented_time();
    bool take_pin_for_mute(uint64_t *operation_id, uint32_t *attempt);
    void accept_pin_result(const PinResult &result);
    void drain_pin();
    void finish_cooldown();
    void sync_pin_idle_hold();
    void arm_connecting_backoff();
    void order_roster(RosterResult *result);
    void remember_login(const char *user_id);
    void refresh_pin_copy();
    void mint_pin_key();
    void wipe_pin_secret();
    void release_pin_view();
    void start_pin_submission(int64_t now_ms);
    void close_pin_operation(const char *terminal, const char *reason);
    void schedule_pin_retry();
    void fail_pin_transport();
    void reset_scoped();
    void clear_scoped_inflight();
    bool take_scoped_for_mute(uint64_t *operation_id, uint32_t *generation, char *kind, int kind_cap);
    void stamp_scoped(TerminalRecord *record, const char *kind);
    void close_scoped(const char *terminal, const char *reason);
    void accept_scoped_result(const ScopedResult &result);
    void drain_scoped();
    void reset_credential();
    void issue_credential(const char *user_id, const char *token, uint64_t pin_operation_id, int64_t now_ms);
    void clear_credential(int64_t now_ms);
    void arm_invalidate(uint64_t pin_operation_id, int64_t now_ms);
    void on_credential_clock();
    int64_t credential_next_event_ms() const;

    int stage_;
    bool wifi_up_;
    int64_t now_ms_;
    int64_t last_user_activity_ms_;
    int64_t last_roster_change_ms_;
    int64_t next_roster_due_ms_;
    int64_t next_heartbeat_due_ms_;
    bool roster_timer_armed_;
    bool heartbeat_armed_;
    bool roster_inflight_;
    bool roster_periodic_;
    bool roster_hold_;
    uint64_t roster_operation_id_;
    uint32_t roster_attempt_;
    int64_t roster_deadline_ms_;
    int stage_at_roster_start_;
    int roster_touch_;
    bool health_unacked_;
    uint64_t health_operation_id_;
    int missed_beats_;
    int stale_;
    int user_count_;
    int selectable_cards_;
    char focus_id_[kMaxId];
    UserId users_[kMaxUsers];
    int focus_stable_;
    int valid_;
    uint32_t generation_;
    uint64_t next_operation_id_;
    char session_id_[16];
    char armed_scenario_[32];
    RosterResult queue_[kRosterResultQueueCapacity];
    int queue_count_;
    int overflow_terminals_;
    int rejected_results_;
    uint64_t overflow_operation_id_;
    TerminalRecord events_[kMaxEvents];
    int event_count_;
    char triggers_[8][24];
    int trigger_count_;
    int pin_phase_;
    int pin_digit_len_;
    char pin_secret_[8];
    char pin_mask_[8];
    char visible_copy_[72];
    char pin_copy_token_[16];
    char selected_user_[kMaxId];
    char pin_fingerprint_[68];
    char pin_reason_[16];
    char pin_scenario_[32];
    uint64_t pin_operation_id_;
    uint32_t pin_frozen_generation_;
    uint32_t pin_key_generation_;
    char pin_frozen_session_[16];
    char pin_frozen_user_[kMaxId];
    bool pin_submitted_;
    int pin_attempt_;
    bool pin_deadline_armed_;
    bool pin_wait_delay_;
    int64_t pin_deadline_ms_;
    int64_t pin_delay_until_ms_;
    int64_t wrong_until_ms_;
    int64_t cooldown_until_ms_;
    int64_t idle_until_ms_;
    bool idle_armed_;
    bool idle_paused_;
    int64_t idle_remaining_ms_;
    int wrong_count_;
    int authenticated_;
    int pin_side_effects_;
    int delay_ms_[3];
    int boot_zero_;
    int boot_partial_;
    int boot_full_;
    int last_update_level_;
    uint32_t property_seq_;
    int shown_cooldown_second_;
    PinResult pin_queue_[kPinResultQueueCapacity];
    int pin_queue_count_;
    int pin_overflow_terminals_;
    int pin_rejected_;
    int signed_in_;
    char signed_user_[kMaxId];
    bool scoped_inflight_;
    uint64_t scoped_operation_id_;
    uint32_t scoped_frozen_generation_;
    char scoped_kind_[16];
    char scoped_user_[kMaxId];
    char scoped_session_[16];
    char scoped_scenario_[32];
    char scoped_reason_[16];
    int scoped_applied_;
    int scoped_trace_applied_;
    int scoped_trace_image_bytes_;
    int scoped_trace_cards_;
    int scoped_trace_dupes_;
    int scoped_trace_http_;
    int scoped_trace_b_side_;
    uint32_t scoped_trace_generation_;
    char scoped_trace_user_[kMaxId];
    ScopedResult scoped_queue_[kScopedResultQueueCapacity];
    int scoped_queue_count_;
    int scoped_overflow_terminals_;
    int scoped_rejected_;
    bool credential_present_;
    bool credential_expired_;
    char credential_token_[kSessionTokenCap];
    char credential_user_[kMaxId];
    uint64_t credential_pin_operation_id_;
    int64_t credential_bound_ms_;
    bool refresh_timer_armed_;
    int64_t next_refresh_due_ms_;
    bool refresh_inflight_;
    uint64_t refresh_operation_id_;
    bool invalidate_armed_;
    int invalidate_attempts_sent_;
    int64_t next_invalidate_due_ms_;
    char invalidate_operation_text_[40];
    char invalidate_session_token_[kSessionTokenCap];
    uint64_t invalidate_pin_operation_id_;
    int health_ack_count_;
    char health_server_time_[36];
    char health_reason_[16];
    char boot_id_[40];
    bool connecting_retry_armed_;
    int connecting_backoff_index_;
    int64_t connecting_retry_due_ms_;
    int roster_timeout_ms_;
    bool server_unavailable_;
    bool roster_updating_;
    bool hangout_known_;
    char recent_ids_[kMaxUsers][kMaxId];
    int recent_count_;
};

void set_user(UserId *user, const char *id);
void fill_users(RosterResult *result, const char *const *ids, int count);
int format_trace_line(const TerminalRecord &record, char *buffer, int capacity);
int format_pin_trace_line(const TerminalRecord &record, char *buffer, int capacity);
int format_scoped_trace_line(const TerminalRecord &record, char *buffer, int capacity);
int format_scoped_observation(const char *scenario, const char *mode, int http_status, int b_side_effects,
                              int revoked, char *buffer, int capacity);

} /* namespace fl_product_login */

#endif
