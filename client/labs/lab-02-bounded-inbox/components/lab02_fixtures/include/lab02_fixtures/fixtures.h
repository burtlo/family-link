#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    const unsigned char *data;
    size_t size;
} lab02_fixture_blob_t;

const lab02_fixture_blob_t *lab02_fixture_selected(void);
const lab02_fixture_blob_t *lab02_fixture_by_name(const char *name);

#ifdef __cplusplus
}
#endif
