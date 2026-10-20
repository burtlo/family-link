#include "failure_publication.hpp"

#include <cassert>
#include <thread>
#include <vector>

int main()
{
    lab04::FailurePublication failure;
    assert(!failure.failed());
    std::vector<std::thread> publishers;
    for (int i = 0; i < 8; ++i) publishers.emplace_back([&failure] { failure.publish_queue_overflow(); });
    for (auto &publisher : publishers) publisher.join();
    assert(failure.failed());
    assert(failure.queue_overflows() == 8);
    // Publication is sticky: the app task cannot miss a previously published
    // failure even when another worker publishes concurrently.
    assert(failure.failed());
    return 0;
}
