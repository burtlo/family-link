#pragma once
#include "lab12_profile_sync.h"
#include <stdbool.h>

bool lab12_fixture_wifi_connect(void);
bool lab12_fixture_get_profile(const char *endpoint, lab12_profile_t *profile);
int lab12_fixture_patch(const char *endpoint, const lab12_profile_update_t *update, lab12_profile_t *canonical, uint32_t *conflict_revision, bool reject, bool drop_response);
bool lab12_fixture_get_probes(char *reject_preview, unsigned reject_size, char *offline_work, unsigned offline_size, char *layout, unsigned layout_size, char *message_color, unsigned message_size);
bool lab12_fixture_get_stats(uint32_t *side_effects);
