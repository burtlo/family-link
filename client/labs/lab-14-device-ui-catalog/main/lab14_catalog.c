#include "lab14_catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t lab14_hex(const char *hex, uint32_t fallback)
{
    if (hex == NULL || hex[0] != '#' || hex[1] == '\0') {
        return fallback;
    }
    return (uint32_t)strtoul(hex + 1, NULL, 16);
}

int lab14_json_int(const cJSON *object, const char *key, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item)) {
        return fallback;
    }
    return item->valueint;
}

const char *lab14_json_str(const cJSON *object, const char *key, const char *fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return fallback;
    }
    return item->valuestring;
}

cJSON *lab14_json_obj(const cJSON *object, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsObject(item) ? item : NULL;
}

cJSON *lab14_slot(const lab14_scene_t *scene, const char *name)
{
    if (scene == NULL || scene->slots == NULL) {
        return NULL;
    }
    cJSON *slot = cJSON_GetObjectItemCaseSensitive(scene->slots, name);
    return cJSON_IsObject(slot) ? slot : NULL;
}

const char *lab14_slot_panel(const lab14_scene_t *scene, const char *name)
{
    return lab14_json_str(lab14_slot(scene, name), "panel", "none");
}

cJSON *lab14_slot_props(const lab14_scene_t *scene, const char *name)
{
    return lab14_json_obj(lab14_slot(scene, name), "props");
}

static uint32_t theme_color(const cJSON *theme, const char *key, uint32_t fallback)
{
    return lab14_hex(lab14_json_str(theme, key, NULL), fallback);
}

static void load_tokens(lab14_tokens_t *tokens, const cJSON *root)
{
    const cJSON *canvas = lab14_json_obj(root, "canvas");
    const cJSON *split = lab14_json_obj(root, "split_pane");
    const cJSON *focused = lab14_json_obj(root, "focused_side");
    const cJSON *theme = lab14_json_obj(root, "theme");
    const cJSON *rail = lab14_json_obj(root, "color_rail");
    const cJSON *type = lab14_json_obj(root, "type");
    const cJSON *card = lab14_json_obj(root, "user_card");
    const cJSON *pin = lab14_json_obj(root, "pin_keypad");
    const cJSON *four = lab14_json_obj(root, "four_button");
    const cJSON *two = lab14_json_obj(root, "label_two_button");
    const cJSON *ts = lab14_json_obj(root, "title_subtitle");
    const cJSON *action = lab14_json_obj(root, "header_action");
    const cJSON *pin_status = lab14_json_obj(root, "pin_status");

    memset(tokens, 0, sizeof(*tokens));
    tokens->canvas_w = lab14_json_int(canvas, "width", 320);
    tokens->canvas_h = lab14_json_int(canvas, "height", 240);
    tokens->split_y = lab14_json_int(split, "split_y", 80);
    tokens->margin = lab14_json_int(split, "margin", 12);
    tokens->split_x = lab14_json_int(focused, "split_x", 240);
    tokens->focused_w = lab14_json_int(focused, "focused_width", 240);
    tokens->side_w = lab14_json_int(focused, "side_width", 80);
    tokens->canvas_radius = lab14_json_int(focused, "canvas_radius", 8);
    tokens->surface = theme_color(theme, "fl-color-surface", 0x101418);
    tokens->surface_upper = theme_color(theme, "fl-color-surface-upper", tokens->surface);
    tokens->surface_lower = theme_color(theme, "fl-color-surface-lower", tokens->surface);
    tokens->text_primary = theme_color(theme, "fl-color-text-primary", 0xE8F0E8);
    tokens->text_secondary = theme_color(theme, "fl-color-text-secondary", 0xA8B0B8);
    tokens->accent = theme_color(theme, "fl-color-accent", 0xE8C040);
    tokens->card_peek = theme_color(theme, "fl-color-card-peek", 0x242C34);
    tokens->control = theme_color(theme, "fl-color-control", 0x3A444E);
    tokens->key_pressed = theme_color(theme, "fl-color-key-pressed", 0x1A2228);
    tokens->sketch_canvas = theme_color(theme, "fl-color-sketch-canvas", 0x000000);
    tokens->swatch_ring = theme_color(theme, "fl-color-swatch-ring", 0xB8BEC6);
    tokens->swatch_d = lab14_json_int(rail, "swatch_diameter", 44);
    tokens->swatch_r = lab14_json_int(rail, "swatch_radius", 22);
    tokens->swatch_border_w = lab14_json_int(rail, "border_width", 2);
    tokens->swatch_outline_w = lab14_json_int(rail, "outline_width", 2);
    tokens->swatch_outline_pad = lab14_json_int(rail, "outline_pad", 1);
    tokens->swatch_count = 0;
    const cJSON *swatches = cJSON_GetObjectItemCaseSensitive(rail, "swatches");
    if (cJSON_IsArray(swatches)) {
        int count = cJSON_GetArraySize(swatches);
        if (count > LAB14_SWATCH_COUNT) {
            count = LAB14_SWATCH_COUNT;
        }
        static const lab14_swatch_t k_default_swatches[LAB14_SWATCH_COUNT] = {
            {269, 208, 0x000000}, {291, 172, 0xF8F8F8}, {269, 136, 0xE8C040},
            {291, 100, 0x4CAF50}, {269, 64, 0xE53935}, {291, 28, 0x5AA0E8},
        };
        for (int i = 0; i < count; i++) {
            const cJSON *item = cJSON_GetArrayItem(swatches, i);
            tokens->swatches[i].cx =
                lab14_json_int(item, "cx", k_default_swatches[i].cx);
            tokens->swatches[i].cy =
                lab14_json_int(item, "cy", k_default_swatches[i].cy);
            tokens->swatches[i].rgb = lab14_hex(
                lab14_json_str(item, "rgb", NULL), k_default_swatches[i].rgb);
        }
        tokens->swatch_count = count;
    }
    if (tokens->swatch_count <= 0) {
        static const lab14_swatch_t k_fallback[LAB14_SWATCH_COUNT] = {
            {269, 208, 0x000000}, {291, 172, 0xF8F8F8}, {269, 136, 0xE8C040},
            {291, 100, 0x4CAF50}, {269, 64, 0xE53935}, {291, 28, 0x5AA0E8},
        };
        memcpy(tokens->swatches, k_fallback, sizeof(k_fallback));
        tokens->swatch_count = LAB14_SWATCH_COUNT;
    }
    tokens->type16 = lab14_json_int(type, "fl-type-16", 16);
    tokens->type24 = lab14_json_int(type, "fl-type-24", 24);
    tokens->type32 = lab14_json_int(type, "fl-type-32", 32);
    tokens->card_w = lab14_json_int(card, "width", 132);
    tokens->card_h = lab14_json_int(card, "height", 133);
    tokens->card_gap = lab14_json_int(card, "gap", 12);
    tokens->card_radius = lab14_json_int(card, "radius", 8);
    tokens->avatar = lab14_json_int(card, "avatar", 66);
    tokens->unfocused_opa = lab14_json_int(card, "unfocused_opacity", 148);
    tokens->unfocused_scale_pct = lab14_json_int(card, "unfocused_scale_pct", 90);
    tokens->pin_pad = lab14_json_int(pin, "pad", 4);
    tokens->pin_gap = lab14_json_int(pin, "gap", 4);
    tokens->pin_w = lab14_json_int(pin, "width", 101);
    tokens->pin_h = lab14_json_int(pin, "height", 48);
    tokens->pin_radius = lab14_json_int(pin, "radius", 8);
    tokens->pin_dim_opa = lab14_json_int(pin, "dim_opa", 128);
    tokens->four_gutter = lab14_json_int(four, "gutter", 16);
    tokens->four_w = lab14_json_int(four, "width", 136);
    tokens->four_h = lab14_json_int(four, "height", 56);
    tokens->two_label_y = lab14_json_int(two, "label_y", 12);
    tokens->two_btn_w = lab14_json_int(two, "button_w", 296);
    tokens->two_btn_h = lab14_json_int(two, "button_h", 48);
    tokens->two_btn_y0 = lab14_json_int(two, "button_y0", 40);
    tokens->two_btn_y1 = lab14_json_int(two, "button_y1", 100);
    tokens->ts_title_x = lab14_json_int(ts, "title_x", 12);
    tokens->ts_title_y = lab14_json_int(ts, "title_y", 12);
    tokens->ts_sub_x = lab14_json_int(ts, "subtitle_x", 12);
    tokens->ts_sub_y = lab14_json_int(ts, "subtitle_y", 41);
    tokens->ts_text_w = lab14_json_int(ts, "text_w", 296);
    tokens->act_title_w = lab14_json_int(action, "title_w", 183);
    tokens->act_sub_w = lab14_json_int(action, "subtitle_w", 183);
    tokens->act_x = lab14_json_int(action, "action_x", 207);
    tokens->act_y = lab14_json_int(action, "action_y", 12);
    tokens->act_w = lab14_json_int(action, "action_w", 101);
    tokens->act_h = lab14_json_int(action, "action_h", 56);
    tokens->pin_avatar = lab14_json_int(pin_status, "avatar", 28);
    snprintf(tokens->pin_prefix, sizeof(tokens->pin_prefix), "%s",
             lab14_json_str(pin_status, "prefix", "Sign in as"));
}

bool lab14_catalog_load(lab14_catalog_t *cat, const char *json, size_t len)
{
    if (cat == NULL || json == NULL || len == 0) {
        return false;
    }
    memset(cat, 0, sizeof(*cat));

    char *copy = malloc(len + 1);
    if (copy == NULL) {
        return false;
    }
    memcpy(copy, json, len);
    copy[len] = '\0';
    cat->root = cJSON_Parse(copy);
    free(copy);
    if (cat->root == NULL) {
        return false;
    }

    load_tokens(&cat->tokens, lab14_json_obj(cat->root, "tokens"));
    cat->title = lab14_json_str(cat->root, "title", "Device UI catalog");

    const cJSON *scenes = cJSON_GetObjectItemCaseSensitive(cat->root, "scenes");
    if (!cJSON_IsArray(scenes) || cJSON_GetArraySize(scenes) < 1) {
        lab14_catalog_free(cat);
        return false;
    }

    cat->scene_count = cJSON_GetArraySize(scenes);
    cat->scenes = calloc((size_t)cat->scene_count, sizeof(*cat->scenes));
    if (cat->scenes == NULL) {
        lab14_catalog_free(cat);
        return false;
    }

    for (int i = 0; i < cat->scene_count; i++) {
        cJSON *item = cJSON_GetArrayItem(scenes, i);
        lab14_scene_t *scene = &cat->scenes[i];
        scene->id = lab14_json_str(item, "id", NULL);
        scene->title = lab14_json_str(item, "title", scene->id);
        scene->group = lab14_json_str(item, "group", "");
        scene->screen = lab14_json_str(item, "screen", "");
        scene->state = lab14_json_str(item, "state", "");
        scene->layout = lab14_json_str(item, "layout", "LAYOUT-SPLIT-PANE");
        scene->notes = lab14_json_str(item, "notes", "");
        scene->slots = lab14_json_obj(item, "slots");
        scene->props = lab14_json_obj(item, "props");
        if (scene->id == NULL) {
            lab14_catalog_free(cat);
            return false;
        }
    }
    return true;
}

void lab14_catalog_free(lab14_catalog_t *cat)
{
    if (cat == NULL) {
        return;
    }
    free(cat->scenes);
    if (cat->root != NULL) {
        cJSON_Delete(cat->root);
    }
    memset(cat, 0, sizeof(*cat));
}

const lab14_scene_t *lab14_catalog_get(const lab14_catalog_t *cat, const char *id)
{
    int index = lab14_catalog_index_of(cat, id);
    if (index < 0) {
        return NULL;
    }
    return &cat->scenes[index];
}

static bool id_matches(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return false;
    }
    while (*left != '\0' && *right != '\0') {
        unsigned char a = (unsigned char)*left;
        unsigned char b = (unsigned char)*right;
        if (a >= 'a' && a <= 'z') {
            a = (unsigned char)(a - ('a' - 'A'));
        }
        if (b >= 'a' && b <= 'z') {
            b = (unsigned char)(b - ('a' - 'A'));
        }
        if (a != b) {
            return false;
        }
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

int lab14_catalog_index_of(const lab14_catalog_t *cat, const char *id)
{
    if (cat == NULL || id == NULL) {
        return -1;
    }
    for (int i = 0; i < cat->scene_count; i++) {
        if (id_matches(cat->scenes[i].id, id)) {
            return i;
        }
    }
    return -1;
}
