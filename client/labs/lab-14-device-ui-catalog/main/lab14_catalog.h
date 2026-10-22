#pragma once

#include "cJSON.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAB14_SWATCH_COUNT 6

typedef struct {
    int cx;
    int cy;
    uint32_t rgb;
} lab14_swatch_t;

typedef struct {
    int canvas_w;
    int canvas_h;
    int split_y;
    int margin;
    int split_x;
    int focused_w;
    int side_w;
    int canvas_radius;
    uint32_t surface;
    uint32_t surface_upper;
    uint32_t surface_lower;
    uint32_t text_primary;
    uint32_t text_secondary;
    uint32_t accent;
    uint32_t card_peek;
    uint32_t control;
    uint32_t key_pressed;
    uint32_t sketch_canvas;
    uint32_t swatch_ring;
    int type16;
    int type24;
    int type32;
    int card_w;
    int card_h;
    int card_gap;
    int card_radius;
    int avatar;
    int unfocused_opa;
    int unfocused_scale_pct;
    int pin_pad;
    int pin_gap;
    int pin_w;
    int pin_h;
    int pin_radius;
    int pin_dim_opa;
    int four_gutter;
    int four_w;
    int four_h;
    int two_label_y;
    int two_btn_w;
    int two_btn_h;
    int two_btn_y0;
    int two_btn_y1;
    int ts_title_x;
    int ts_title_y;
    int ts_sub_x;
    int ts_sub_y;
    int ts_text_w;
    int act_title_w;
    int act_sub_w;
    int act_x;
    int act_y;
    int act_w;
    int act_h;
    int pin_avatar;
    char pin_prefix[24];
    int swatch_d;
    int swatch_r;
    int swatch_border_w;
    int swatch_outline_w;
    int swatch_outline_pad;
    int swatch_count;
    lab14_swatch_t swatches[LAB14_SWATCH_COUNT];
} lab14_tokens_t;

typedef struct {
    const char *id;
    const char *title;
    const char *group;
    const char *screen;
    const char *state;
    const char *layout;
    const char *notes;
    cJSON *slots;
    cJSON *props;
} lab14_scene_t;

typedef struct {
    cJSON *root;
    lab14_tokens_t tokens;
    lab14_scene_t *scenes;
    int scene_count;
    const char *title;
} lab14_catalog_t;

uint32_t lab14_hex(const char *hex, uint32_t fallback);
int lab14_json_int(const cJSON *object, const char *key, int fallback);
const char *lab14_json_str(const cJSON *object, const char *key, const char *fallback);
cJSON *lab14_json_obj(const cJSON *object, const char *key);
cJSON *lab14_slot(const lab14_scene_t *scene, const char *name);
const char *lab14_slot_panel(const lab14_scene_t *scene, const char *name);
cJSON *lab14_slot_props(const lab14_scene_t *scene, const char *name);

bool lab14_catalog_load(lab14_catalog_t *cat, const char *json, size_t len);
void lab14_catalog_free(lab14_catalog_t *cat);
const lab14_scene_t *lab14_catalog_get(const lab14_catalog_t *cat, const char *id);
int lab14_catalog_index_of(const lab14_catalog_t *cat, const char *id);
