#ifndef LAB07_SESSION_HPP
#define LAB07_SESSION_HPP

#include "stages.hpp"

#include <stdint.h>

namespace lab07 {

enum {
    kRosterResultQueueCapacity = 4,
    kActiveRosterPeriodMs = 60000,
    kIdleQuietMs = 600000,
    kIdleRosterPeriodMs = 300000,
    kHeartbeatPeriodMs = 60000,
    kRequestTimeoutMs = 1000,
    kStaleAfterMissedBeats = 3,
    kMaxUsers = 8,
    kMaxId = 16,
    kMaxEvents = 96
};

struct UserId {
    char id[kMaxId];
};

struct RosterResult {
    uint64_t operation_id;
    int http_status;
    int valid;
    int parse_ok;
    int user_count;
    UserId users[kMaxUsers];
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
};

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
    void note_touch_during_wait();
    void note_transport_failure();
    void ack_health();
    bool enqueue_roster(const RosterResult &result);
    void drain();
    void submit_roster(const RosterResult &result, int64_t now_ms);
    void advance_to(int64_t now_ms);
    void arm_scenario(const char *scenario);
    void publish(const char *scenario, const char *terminal);
    void annotate_last(const char *parsed_keys, int grace_ms, int clear_stale);

    int stage() const;
    const char *stage_name() const;
    int user_count() const;
    const char *focus_id() const;
    const char *user_at(int index) const;
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
    int terminal_count_for(uint64_t operation_id) const;
    const char *terminal_for(uint64_t operation_id) const;
    int event_count() const;
    const TerminalRecord &event_at(int index) const;
    uint32_t generation() const;
    const char *session_id() const;

private:
    void reset_runtime();
    void apply_stage(int stage, const char *trigger);
    void begin_roster(int64_t now_ms, bool periodic, bool hold);
    void begin_health(int64_t now_ms, const char *trigger);
    void clear_roster_inflight();
    void on_roster_timeout();
    void on_heartbeat_due();
    void on_roster_due();
    void apply_roster_body(const RosterResult &result);
    bool same_membership(const RosterResult &result) const;
    void preserve_focus(const RosterResult &result);
    bool record_terminal(uint64_t operation_id, uint32_t attempt, const char *kind,
                         const char *terminal);
    void fill_snapshot(TerminalRecord *record) const;
    void remember_users(const RosterResult &result);
    int64_t quiet_anchor_ms() const;
    void pull_roster_period_forward(int64_t now_ms);

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
};

enum ManualAction {
    kManualWifiDown = 1,
    kManualWifiReturn,
    kManualEmpty,
    kManualOne,
    kManualEight,
    kManualAdd,
    kManualRemove,
    kManualReorder,
    kManualFailOnce,
    kManualFailTwice,
    kManualInvalid,
    kManualHold,
    kManualMute,
    kManualUnmute,
    kManualIdleClock,
    kManualStaleBeats
};

void set_user(UserId *user, const char *id);
void fill_users(RosterResult *result, const char *const *ids, int count);
int format_trace_line(const TerminalRecord &record, char *buffer, int capacity);
void run_scripted_catalog(SessionWriter *session);
/* Returns the hold deadline in model milliseconds, or -1 when the hold did not start.
   Other actions return 0. */
int64_t apply_manual_control(SessionWriter *session, int action);
void complete_manual_hold(SessionWriter *session, int64_t deadline_ms);

} /* namespace lab07 */

#endif
