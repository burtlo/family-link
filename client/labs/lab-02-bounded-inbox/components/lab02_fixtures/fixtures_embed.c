#include "lab02_fixtures/fixtures.h"

#include "sdkconfig.h"
#include <stdbool.h>
#include <string.h>

extern const unsigned char _binary_inbox_8_json_start[];
extern const unsigned char _binary_inbox_8_json_end[];
extern const unsigned char _binary_inbox_16_json_start[];
extern const unsigned char _binary_inbox_16_json_end[];
extern const unsigned char _binary_inbox_64_json_start[];
extern const unsigned char _binary_inbox_64_json_end[];
extern const unsigned char _binary_inbox_256_json_start[];
extern const unsigned char _binary_inbox_256_json_end[];

static lab02_fixture_blob_t s_fixtures[4];
static bool s_ready;

static void fixtures_ensure(void)
{
    if (s_ready) {
        return;
    }
    s_fixtures[0].name = "inbox_8";
    s_fixtures[0].data = _binary_inbox_8_json_start;
    s_fixtures[0].size = (size_t)(_binary_inbox_8_json_end - _binary_inbox_8_json_start);
    s_fixtures[1].name = "inbox_16";
    s_fixtures[1].data = _binary_inbox_16_json_start;
    s_fixtures[1].size = (size_t)(_binary_inbox_16_json_end - _binary_inbox_16_json_start);
    s_fixtures[2].name = "inbox_64";
    s_fixtures[2].data = _binary_inbox_64_json_start;
    s_fixtures[2].size = (size_t)(_binary_inbox_64_json_end - _binary_inbox_64_json_start);
    s_fixtures[3].name = "inbox_256";
    s_fixtures[3].data = _binary_inbox_256_json_start;
    s_fixtures[3].size = (size_t)(_binary_inbox_256_json_end - _binary_inbox_256_json_start);
    s_ready = true;
}

const lab02_fixture_blob_t *lab02_fixture_by_name(const char *name)
{
    fixtures_ensure();
    for (size_t i = 0; i < sizeof(s_fixtures) / sizeof(s_fixtures[0]); i++) {
        if (name && s_fixtures[i].name && strcmp(name, s_fixtures[i].name) == 0) {
            return &s_fixtures[i];
        }
    }
    return NULL;
}

const lab02_fixture_blob_t *lab02_fixture_selected(void)
{
    fixtures_ensure();
#if CONFIG_LAB02_FIXTURE_8
    return &s_fixtures[0];
#elif CONFIG_LAB02_FIXTURE_16
    return &s_fixtures[1];
#elif CONFIG_LAB02_FIXTURE_64
    return &s_fixtures[2];
#elif CONFIG_LAB02_FIXTURE_256
    return &s_fixtures[3];
#else
    return &s_fixtures[1];
#endif
}
