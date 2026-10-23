#include "portraits_registry.h"
#include <string.h>

extern const lv_image_dsc_t portrait_avatar_an01_candidate;
extern const lv_image_dsc_t portrait_avatar_an02_candidate;
extern const lv_image_dsc_t portrait_avatar_an03_candidate;
extern const lv_image_dsc_t portrait_avatar_an04_candidate;
extern const lv_image_dsc_t portrait_avatar_an05_candidate;
extern const lv_image_dsc_t portrait_avatar_an06_candidate;
extern const lv_image_dsc_t portrait_avatar_an07_candidate;
extern const lv_image_dsc_t portrait_avatar_an08_candidate;
extern const lv_image_dsc_t portrait_avatar_an09_candidate;
extern const lv_image_dsc_t portrait_avatar_an10_candidate;
extern const lv_image_dsc_t portrait_avatar_an11_candidate;
extern const lv_image_dsc_t portrait_avatar_fa01_candidate;
extern const lv_image_dsc_t portrait_avatar_fa02_candidate;
extern const lv_image_dsc_t portrait_avatar_fa03_candidate;
extern const lv_image_dsc_t portrait_avatar_fa04_candidate;
extern const lv_image_dsc_t portrait_avatar_fa05_candidate;
extern const lv_image_dsc_t portrait_avatar_fa06_candidate;
extern const lv_image_dsc_t portrait_avatar_fa07_candidate;
extern const lv_image_dsc_t portrait_avatar_fa08_candidate;
extern const lv_image_dsc_t portrait_avatar_fa09_candidate;
extern const lv_image_dsc_t portrait_avatar_fd01_candidate;
extern const lv_image_dsc_t portrait_avatar_fd02_candidate;
extern const lv_image_dsc_t portrait_avatar_fd03_candidate;
extern const lv_image_dsc_t portrait_avatar_fd04_candidate;
extern const lv_image_dsc_t portrait_avatar_fd05_candidate;
extern const lv_image_dsc_t portrait_avatar_fd06_candidate;
extern const lv_image_dsc_t portrait_avatar_mk01_candidate;
extern const lv_image_dsc_t portrait_avatar_rb01_candidate;
extern const lv_image_dsc_t portrait_avatar_rb02_candidate;
extern const lv_image_dsc_t portrait_avatar_rb03_candidate;
extern const lv_image_dsc_t portrait_avatar_rb04_candidate;
extern const lv_image_dsc_t portrait_avatar_rb05_candidate;
extern const lv_image_dsc_t portrait_avatar_sp01_candidate;
extern const lv_image_dsc_t portrait_avatar_we01_candidate;
extern const lv_image_dsc_t portrait_avatar_we02_candidate;
extern const lv_image_dsc_t portrait_avatar_we03_candidate;
extern const lv_image_dsc_t portrait_avatar_we04_candidate;
extern const lv_image_dsc_t portrait_avatar_we05_candidate;

typedef struct {
    const char *id;
    const lv_image_dsc_t *image;
} fl_portrait_entry_t;

static const fl_portrait_entry_t s_portraits[] = {
    {"avatar-an01", &portrait_avatar_an01_candidate},
    {"avatar-an02", &portrait_avatar_an02_candidate},
    {"avatar-an03", &portrait_avatar_an03_candidate},
    {"avatar-an04", &portrait_avatar_an04_candidate},
    {"avatar-an05", &portrait_avatar_an05_candidate},
    {"avatar-an06", &portrait_avatar_an06_candidate},
    {"avatar-an07", &portrait_avatar_an07_candidate},
    {"avatar-an08", &portrait_avatar_an08_candidate},
    {"avatar-an09", &portrait_avatar_an09_candidate},
    {"avatar-an10", &portrait_avatar_an10_candidate},
    {"avatar-an11", &portrait_avatar_an11_candidate},
    {"avatar-fa01", &portrait_avatar_fa01_candidate},
    {"avatar-fa02", &portrait_avatar_fa02_candidate},
    {"avatar-fa03", &portrait_avatar_fa03_candidate},
    {"avatar-fa04", &portrait_avatar_fa04_candidate},
    {"avatar-fa05", &portrait_avatar_fa05_candidate},
    {"avatar-fa06", &portrait_avatar_fa06_candidate},
    {"avatar-fa07", &portrait_avatar_fa07_candidate},
    {"avatar-fa08", &portrait_avatar_fa08_candidate},
    {"avatar-fa09", &portrait_avatar_fa09_candidate},
    {"avatar-fd01", &portrait_avatar_fd01_candidate},
    {"avatar-fd02", &portrait_avatar_fd02_candidate},
    {"avatar-fd03", &portrait_avatar_fd03_candidate},
    {"avatar-fd04", &portrait_avatar_fd04_candidate},
    {"avatar-fd05", &portrait_avatar_fd05_candidate},
    {"avatar-fd06", &portrait_avatar_fd06_candidate},
    {"avatar-mk01", &portrait_avatar_mk01_candidate},
    {"avatar-rb01", &portrait_avatar_rb01_candidate},
    {"avatar-rb02", &portrait_avatar_rb02_candidate},
    {"avatar-rb03", &portrait_avatar_rb03_candidate},
    {"avatar-rb04", &portrait_avatar_rb04_candidate},
    {"avatar-rb05", &portrait_avatar_rb05_candidate},
    {"avatar-sp01", &portrait_avatar_sp01_candidate},
    {"avatar-we01", &portrait_avatar_we01_candidate},
    {"avatar-we02", &portrait_avatar_we02_candidate},
    {"avatar-we03", &portrait_avatar_we03_candidate},
    {"avatar-we04", &portrait_avatar_we04_candidate},
    {"avatar-we05", &portrait_avatar_we05_candidate},
};

const lv_image_dsc_t *fl_portrait_lookup(const char *portrait_id)
{
    if (portrait_id == NULL || portrait_id[0] == '\0') {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(s_portraits) / sizeof(s_portraits[0]); i++) {
        if (strcmp(portrait_id, s_portraits[i].id) == 0) {
            return s_portraits[i].image;
        }
    }
    return NULL;
}
