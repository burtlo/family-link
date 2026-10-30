#include "product_settings_service.hpp"

namespace product_ui
{
SettingsService::SettingsService(SettingsTransport transport) : transport_(transport)
{
#ifdef ESP_PLATFORM
    if (!transport_.start) transport_.start = settings_worker::start;
    if (!transport_.submit) transport_.submit = settings_worker::submit;
    if (!transport_.poll) transport_.poll = settings_worker::poll;
#endif
}

SettingsSnapshot SettingsService::snapshot() const { return {stop_, ready_, leave_pending_}; }
int SettingsService::codecVolume() const { return fl_product_volume::volume_codec_for_stop_index(stop_); }

bool SettingsService::setStop(int index)
{
    if (!ready_ || leave_pending_) return false;
    index = fl_product_volume::volume_stop_index_clamp(index);
    if (stop_ != index) { stop_ = index; apply_due_ = now_; }
    return true;
}

bool SettingsService::prepareLeave(SettingsLeaveReason reason)
{
    if (completion_pending_) {
        if (reason == SettingsLeaveReason::Mute) completion_.reason = reason;
        return false;
    }
    if (leave_pending_) {
        // Mute supersedes a BOOT return even if its save is already in flight.
        if (reason == SettingsLeaveReason::Mute) reason_ = reason;
        return false;
    }
    reason_ = reason;
    leave_pending_ = true;
    return true;
}

bool SettingsService::takeLeaveCompletion(SettingsLeaveCompletion &completion)
{
    if (!completion_pending_) return false;
    completion = completion_;
    completion_pending_ = false;
    return true;
}

bool SettingsService::send(SettingsJobKind kind, int stop)
{
    SettingsJob job;
    job.operation = ++next_operation_;
    if (!job.operation) job.operation = ++next_operation_;
    job.kind = kind;
    job.stop = stop;
    if (!transport_.submit || !transport_.submit(job)) {
        if (kind == SettingsJobKind::Save) {
            completion_ = {reason_, false};
            completion_pending_ = true;
            leave_pending_ = false;
        }
        retry_due_ = now_ + 1000;
        return false;
    }
    active_ = job;
    active_deadline_ = now_ + kWorkerDeadlineMs;
    return true;
}

void SettingsService::consume(const SettingsResult &result)
{
    if (!active_.operation || result.operation != active_.operation || result.kind != active_.kind) return;
    const auto kind = active_.kind;
    const int frozen_stop = active_.stop;
    active_.operation = 0;
    active_deadline_ = 0;
    if (kind == SettingsJobKind::Load) {
        stop_ = result.success ? fl_product_volume::volume_stop_index_clamp(result.stop) :
                                 fl_product_volume::volume_stop_index_default();
        ready_ = true;
    } else if (kind == SettingsJobKind::Apply) {
        if (result.success) applied_stop_ = frozen_stop;
        else apply_due_ = now_ + 1000;
    } else {
        completion_ = {reason_, result.success};
        completion_pending_ = true;
        leave_pending_ = false;
    }
}

void SettingsService::advance(int64_t now)
{
    if (now > now_) now_ = now;
    if (!started_ && now_ >= retry_due_) {
        started_ = transport_.start && transport_.start();
        if (!started_) retry_due_ = now_ + 1000;
    }
    if (!started_) {
        if (leave_pending_) {
            completion_ = {reason_, false};
            completion_pending_ = true;
            leave_pending_ = false;
        }
        return;
    }
    // Expire before draining results: a completion arriving at or after the
    // deadline cannot revive an expired operation or overwrite a newer intent.
    if (active_.operation && now_ >= active_deadline_) {
        consume({active_.operation, active_.kind, active_.stop, false});
    }
    if (transport_.poll) {
        SettingsResult result;
        for (int i = 0; i < 4 && transport_.poll(result); ++i) consume(result);
    }
    if (active_.operation || now_ < retry_due_) return;
    if (!ready_) send(SettingsJobKind::Load, stop_);
    else if (leave_pending_) send(SettingsJobKind::Save, stop_);
    else if (applied_stop_ != stop_ && now_ >= apply_due_) send(SettingsJobKind::Apply, stop_);
}
} // namespace product_ui
