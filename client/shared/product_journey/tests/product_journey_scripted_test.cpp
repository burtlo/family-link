#include "fl_product_journey.hpp"

#include <stdio.h>
#include <string.h>

namespace {

int g_failures = 0;

void check(bool ok, const char *text, int line)
{
    if (!ok) {
        fprintf(stderr, "FAIL product_journey_scripted_test.cpp:%d %s\n", line, text);
        g_failures += 1;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

int run_all_l1()
{
    const char *scenarios[] = {
        "boot-volume-mute",
        "roster-during-pin",
        "inbox-snapshot-during-playback",
        "refresh-during-record-volume",
        "send-while-inbox-changes",
        "mute-during-media-fetch",
        "boot-during-pending-send",
    };
    unsigned index;
    for (index = 0; index < sizeof(scenarios) / sizeof(scenarios[0]); ++index) {
        const int failures = fl_product_journey::run_scripted_scenario(scenarios[index]);
        CHECK(failures == 0);
        g_failures += failures;
    }
    return g_failures;
}

} /* namespace */

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "all";
    if (strcmp(name, "all") == 0) {
        return run_all_l1() == 0 ? 0 : 1;
    }
    return fl_product_journey::run_scripted_scenario(name) == 0 ? 0 : 1;
}
