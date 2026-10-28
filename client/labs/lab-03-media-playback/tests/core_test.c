#include "media_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    uint8_t data[L3_SKETCH_CAP];
    size_t n = fread(data, 1, sizeof(data), f);
    fclose(f);
    uint16_t count = 0;
    assert(l3_validate_sketch(data, n, &count) && count == 4096);
    uint16_t image[14400], ref[14400];
    memset(image, 255, sizeof(image));
    memset(ref, 255, sizeof(ref));
    unsigned rendered = 0;
    // Worst catch-up is bounded per call; completion preserves every ordered stroke.
    while (rendered < count)
        assert(l3_render(image, data, count, &rendered, 3000) <= L3_WORK_CAP);
    for (unsigned i = 0; i < count; i++)
        l3_draw(ref, data, i);
    assert(memcmp(image, ref, sizeof(image)) == 0);
    unsigned paused = rendered;
    assert(l3_render(image, data, count, &rendered, 1500) == 0 && rendered == paused);
    assert(!l3_validate_sketch(data, n - 1, &count));
    data[12] = 120;
    assert(!l3_validate_sketch(data, n, &count));
    puts("core bounded dense replay, pause, malformed input: PASS");
}
