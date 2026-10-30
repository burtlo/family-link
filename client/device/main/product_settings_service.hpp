#pragma once

#include <cstdint>
#include "fl_product_volume_stops.hpp"

namespace product_ui
{
enum class SettingsJobKind { Load, Apply, Save };
struct SettingsJob {
    uint32_t operation = 0;
    SettingsJobKind kind = SettingsJobKind::Load;
    int stop = 12;
};
struct SettingsResult {
    uint32_t operation = 0;
    SettingsJobKind kind = SettingsJobKind::Load;
    int stop = 12;
    bool success = false;
};
struct SettingsTransport {
    bool (*start)() = nullptr;
    bool (*submit)(const SettingsJob &) = nullptr;
    bool (*poll)(SettingsResult &) = nullptr;
};
enum class SettingsLeaveReason { Boot, Mute };
struct SettingsLeaveCompletion {
    SettingsLeaveReason reason = SettingsLeaveReason::Boot;
    bool saved = false;
};
struct SettingsSnapshot {
    int stop = 12;
    bool ready = false;
    bool saving = false;
};

// Endpoint lifetime. All hardware and persistence work is queued by value to
// the worker; no controller or LVGL pointer crosses that boundary.
class SettingsService
{
  public:
    // Engineering bound for worker acknowledgement, not a product-visible timer.
    static constexpr int64_t kWorkerDeadlineMs = 10000;
    explicit SettingsService(SettingsTransport transport = {});
    void advance(int64_t now);
    bool setStop(int index);
    bool prepareLeave(SettingsLeaveReason reason);
    bool takeLeaveCompletion(SettingsLeaveCompletion &completion);
    SettingsSnapshot snapshot() const;
    int codecVolume() const;

  private:
    bool send(SettingsJobKind kind, int stop);
    void consume(const SettingsResult &result);
    SettingsTransport transport_;
    SettingsJob active_;
    uint32_t next_operation_ = 0;
    int stop_ = 12;
    int applied_stop_ = -1;
    bool started_ = false;
    bool ready_ = false;
    bool leave_pending_ = false;
    bool completion_pending_ = false;
    SettingsLeaveReason reason_ = SettingsLeaveReason::Boot;
    SettingsLeaveCompletion completion_;
    int64_t now_ = 0;
    int64_t retry_due_ = 0;
    int64_t apply_due_ = 0;
    int64_t active_deadline_ = 0;
};

namespace settings_worker
{
bool start();
bool submit(const SettingsJob &job);
bool poll(SettingsResult &result);
}
} // namespace product_ui
