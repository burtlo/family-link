#include "product_settings_service.hpp"
#include "product_settings_view_controller.hpp"
#include <cassert>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

namespace
{
using namespace product_ui;
std::deque<SettingsJob> jobs;
std::deque<SettingsResult> results;
std::vector<int> outputs;
bool worker_available;
bool refuse;
bool fail_save;
bool fail_apply;
int persisted;
int saves;
int64_t now;
bool start() { return worker_available; }
bool submit(const SettingsJob &job)
{
    if (refuse) return false;
    jobs.push_back(job);
    return true;
}
bool poll(SettingsResult &result)
{
    if (results.empty()) return false;
    result = results.front();
    results.pop_front();
    return true;
}
void workerStep()
{
    assert(!jobs.empty());
    const auto job = jobs.front();
    jobs.pop_front();
    SettingsResult result;
    result.kind = job.kind;
    result.operation = job.operation;
    result.stop = job.stop;
    if (job.kind == SettingsJobKind::Load) {
        result.success = persisted >= 0;
        result.stop = persisted;
    } else if (job.kind == SettingsJobKind::Save) {
        ++saves;
        result.success = !fail_save;
        if (result.success) persisted = job.stop;
    } else {
        result.success = !fail_apply;
        outputs.push_back(fl_product_volume::volume_codec_for_stop_index(job.stop));
    }
    results.push_back(result);
}
void initialize(SettingsService &service)
{
    service.advance(++now);
    assert(!service.snapshot().ready && jobs.front().kind == SettingsJobKind::Load);
    workerStep();
    service.advance(++now);
    assert(service.snapshot().ready && jobs.front().kind == SettingsJobKind::Apply);
    workerStep();
    service.advance(++now);
    assert(jobs.empty());
}
void finish(SettingsService &service)
{
    for (int i = 0; i < 5; ++i) {
        service.advance(++now);
        if (jobs.empty()) return;
        workerStep();
    }
    assert(false);
}
void reset()
{
    jobs.clear(); results.clear(); outputs.clear();
    persisted = -1; saves = 0; now = 0;
    worker_available = true;
    refuse = fail_save = fail_apply = false;
}
}

void testSettingsControllers(lv_obj_t *content)
{
    using namespace product_ui;
    reset();
    {
        SettingsService service({start, submit, poll});
        assert(service.snapshot().stop == 12 && service.codecVolume() == 100);
        assert(!service.setStop(3)); // The async persisted value wins over edits before initialization.
        initialize(service);
        assert(outputs.size() == 1 && outputs.back() == 100);
        const int expected[] = {0, 78, 80, 82, 84, 86, 88, 90, 92, 94, 96, 98, 100};
        for (int i = 0; i < 13; ++i) {
            assert(service.setStop(i));
            assert(service.snapshot().stop == i && service.codecVolume() == expected[i]);
            // setStop is an intent. The worker has not applied anything yet.
            const auto before = outputs.size();
            service.advance(++now);
            assert(outputs.size() == before);
            workerStep();
            service.advance(++now);
            assert(outputs.back() == expected[i]);
        }
        service.setStop(-100);
        assert(service.snapshot().stop == 0);
        finish(service);
        service.setStop(100);
        assert(service.snapshot().stop == 12);
        finish(service);
        service.setStop(1); service.setStop(2); service.setStop(3);
        service.advance(++now);
        assert(jobs.size() == 1 && jobs.front().stop == 3);
        workerStep();
        service.advance(++now);
        assert(service.prepareLeave(SettingsLeaveReason::Boot));
        assert(!service.prepareLeave(SettingsLeaveReason::Boot));
        assert(!service.setStop(8));
        assert(saves == 0);
        service.advance(++now);
        assert(saves == 0 && jobs.front().kind == SettingsJobKind::Save);
        workerStep();
        service.advance(++now);
        SettingsLeaveCompletion completion;
        assert(service.takeLeaveCompletion(completion));
        assert(completion.reason == SettingsLeaveReason::Boot && completion.saved && persisted == 3);
        assert(!service.takeLeaveCompletion(completion));
        service.setStop(5);
        finish(service);
        fail_save = true;
        service.prepareLeave(SettingsLeaveReason::Boot);
        service.advance(++now);
        workerStep();
        service.advance(++now);
        assert(service.takeLeaveCompletion(completion) && !completion.saved);
        assert(service.snapshot().stop == 5 && persisted == 3);
        fail_save = false;
        // Mute supersedes an already submitted BOOT save, without a second write.
        service.prepareLeave(SettingsLeaveReason::Boot);
        service.advance(++now);
        service.prepareLeave(SettingsLeaveReason::Mute);
        workerStep();
        service.advance(++now);
        assert(service.takeLeaveCompletion(completion));
        assert(completion.reason == SettingsLeaveReason::Mute && completion.saved && persisted == 5);
    }
    {
        SettingsService restarted({start, submit, poll});
        initialize(restarted);
        assert(restarted.snapshot().stop == 5 && restarted.codecVolume() == 86);
        RootViewController root(content);
        auto *controller = new SettingsViewController(restarted);
        assert(root.replace(std::unique_ptr<ViewController>(controller), false));
        lv_obj_update_layout(content);
        assert(std::strcmp(lv_label_get_text(controller->heading()), "Volume") == 0);
        assert(lv_obj_get_child_count(controller->upperSlot()) == 1);
        assert(lv_obj_get_child_count(controller->lowerSlot()) == 1);
        assert(lv_obj_get_child_count(controller->slider()) == 0); // No numeric readout.
        assert(lv_slider_get_min_value(controller->slider()) == 0);
        assert(lv_slider_get_max_value(controller->slider()) == 12);
        assert(!controller->handlesRed());
        root.dispatchRed();
        assert(!restarted.snapshot().saving);
        for (int i = 0; i < 13; ++i) {
            lv_slider_set_value(controller->slider(), i, LV_ANIM_OFF);
            lv_obj_send_event(controller->slider(), LV_EVENT_VALUE_CHANGED, nullptr);
            assert(restarted.snapshot().stop == i);
            finish(restarted);
        }
        lv_mem_monitor_t before, after;
        lv_mem_monitor(&before);
        for (int i = 0; i < 20; ++i) root.render();
        lv_mem_monitor(&after);
        assert(before.free_size == after.free_size);
        root.dispatchBoot();
        root.render();
        assert(restarted.snapshot().saving && lv_obj_has_state(controller->slider(), LV_STATE_DISABLED));
        // Remove the view before persistence completes (mute path). Worker and service survive.
        assert(root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController), false));
        restarted.prepareLeave(SettingsLeaveReason::Mute);
        finish(restarted);
        SettingsLeaveCompletion completion;
        assert(restarted.takeLeaveCompletion(completion));
        assert(completion.reason == SettingsLeaveReason::Mute && completion.saved);
        auto *returned = new SettingsViewController(restarted);
        assert(root.replace(std::unique_ptr<ViewController>(returned), false));
        assert(lv_slider_get_value(returned->slider()) == 12);
        assert(lv_obj_get_child_count(returned->lowerSlot()) == 1); // Never connectivity status.
    }
    reset();
    {
        SettingsService service({start, submit, poll});
        initialize(service);
        refuse = true;
        service.prepareLeave(SettingsLeaveReason::Boot);
        service.advance(++now);
        SettingsLeaveCompletion completion;
        assert(service.takeLeaveCompletion(completion) && !completion.saved);
        assert(!service.snapshot().saving);
    }
    reset();
    {
        SettingsService service({start, submit, poll});
        service.advance(++now);
        const auto load = jobs.front(); jobs.pop_front(); // Accepted, never acknowledged.
        now += SettingsService::kWorkerDeadlineMs - 1;
        service.advance(now);
        assert(!service.snapshot().ready && jobs.empty());
        service.advance(++now);
        assert(service.snapshot().ready && service.snapshot().stop == 12);
        assert(jobs.size() == 1 && jobs.front().kind == SettingsJobKind::Apply);
        results.push_back({load.operation, load.kind, 2, true});
        service.advance(++now);
        assert(service.snapshot().stop == 12); // Late load cannot overwrite fallback.
        const auto apply = jobs.front(); jobs.pop_front();
        service.setStop(4);
        now += SettingsService::kWorkerDeadlineMs;
        service.advance(now);
        assert(jobs.empty()); // Failed apply retries with backoff, never per-tick spin.
        service.advance(now + 999);
        assert(jobs.empty());
        now += 1000;
        service.advance(now);
        assert(jobs.size() == 1 && jobs.front().stop == 4);
        results.push_back({apply.operation, apply.kind, apply.stop, true});
        service.advance(++now);
        assert(jobs.size() == 1 && service.snapshot().stop == 4);
        workerStep(); service.advance(++now);
        service.prepareLeave(SettingsLeaveReason::Boot);
        service.advance(++now);
        const auto save = jobs.front(); jobs.pop_front();
        now += SettingsService::kWorkerDeadlineMs - 1;
        service.advance(now);
        SettingsLeaveCompletion completion;
        assert(service.snapshot().saving && !service.takeLeaveCompletion(completion));
        service.advance(++now);
        assert(service.takeLeaveCompletion(completion) && !completion.saved);
        assert(completion.reason == SettingsLeaveReason::Boot);
        assert(!service.snapshot().saving && service.snapshot().stop == 4);
        assert(!service.takeLeaveCompletion(completion));
        service.setStop(7);
        service.advance(++now);
        results.push_back({save.operation, save.kind, save.stop, true});
        service.advance(++now);
        assert(!service.takeLeaveCompletion(completion) && service.snapshot().stop == 7);
        finish(service);
        service.prepareLeave(SettingsLeaveReason::Mute);
        finish(service);
        assert(service.takeLeaveCompletion(completion) && completion.saved);
        assert(completion.reason == SettingsLeaveReason::Mute && persisted == 7);
    }
    reset();
    {
        SettingsService service({start, submit, poll});
        worker_available = false;
        service.prepareLeave(SettingsLeaveReason::Mute);
        service.advance(++now);
        SettingsLeaveCompletion completion;
        assert(service.takeLeaveCompletion(completion) && !completion.saved);
    }
    reset();
}
