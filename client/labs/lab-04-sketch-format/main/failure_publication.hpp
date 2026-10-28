#pragma once

#include <atomic>
#include <cstdint>

namespace lab04
{

// Worker and callback failures are published to the app task without a data
// race. Once set, the latch stays set for the lifetime of the harness run.
class FailurePublication
{
  public:
    void publish() noexcept
    {
        failed_.store(true, std::memory_order_release);
    }
    void publish_queue_overflow() noexcept
    {
        queue_overflows_.fetch_add(1, std::memory_order_relaxed);
        publish();
    }
    bool failed() const noexcept
    {
        return failed_.load(std::memory_order_acquire);
    }
    std::uint32_t queue_overflows() const noexcept
    {
        return queue_overflows_.load(std::memory_order_relaxed);
    }

  private:
    std::atomic<bool> failed_{false};
    std::atomic<std::uint32_t> queue_overflows_{0};
};

} // namespace lab04
