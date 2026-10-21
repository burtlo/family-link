#include "lab14_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lab14_catalog_t *g_catalog;
static lv_obj_t *g_root;
static const lab14_scene_t *g_scene;
static bool g_showing_index;
static lv_timer_t *g_dots_timer;
static lv_obj_t *g_dots_label;
static int g_dot_frame;
static char g_current_id[48];

static const lv_font_t *font_px(int px)
{
    if (px >= 32) {
        return &lv_font_montserrat_32;
    }
    if (px >= 24) {
        return &lv_font_montserrat_24;
    }
    return &lv_font_montserrat_16;
}

static void style_label(lv_obj_t *label, uint32_t color, int px)
{
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, font_px(px), 0);
}

static void paint_plain(lv_obj_t *obj, uint32_t color)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_scrollable(obj, false);
}

static lv_obj_t *make_slot(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *slot = lv_obj_create(parent);
    lv_obj_set_pos(slot, x, y);
    lv_obj_set_size(slot, w, h);
    paint_plain(slot, color);
    return slot;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                            int type_px, lv_event_cb_t cb, void *user)
{
    const lab14_tokens_t *t = &g_catalog->tokens;
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, lv_color_hex(t->control), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(t->key_pressed), LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, t->card_radius, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_center(label);
    style_label(label, t->text_primary, type_px);
    if (cb != NULL) {
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user);
    }
    return button;
}

static void stop_dots(void)
{
    if (g_dots_timer != NULL) {
        lv_timer_delete(g_dots_timer);
        g_dots_timer = NULL;
    }
    g_dots_label = NULL;
    g_dot_frame = 0;
}

static void dots_cb(lv_timer_t *timer)
{
    (void)timer;
    if (g_dots_label == NULL) {
        return;
    }
    g_dot_frame = (g_dot_frame + 1) % 3;
    if (g_dot_frame == 0) {
        lv_label_set_text(g_dots_label, ".  ");
    } else if (g_dot_frame == 1) {
        lv_label_set_text(g_dots_label, ". .");
    } else {
        lv_label_set_text(g_dots_label, ". . .");
    }
}

static void tap_notice(lv_event_t *event)
{
    const char *name = (const char *)lv_event_get_user_data(event);
    printf("LAB14_TAP control=%s scene=%s\n", name != NULL ? name : "?", g_current_id);
}

static void set_json_int(cJSON *object, const char *key, int value)
{
    if (object == NULL) {
        return;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(item)) {
        cJSON_SetIntValue(item, value);
    } else {
        cJSON_AddNumberToObject(object, key, value);
    }
}

static void remount_current(void)
{
    if (g_current_id[0] != '\0') {
        lab14_ui_show_scene(g_current_id);
    }
}

static void focus_card(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    cJSON *props = lab14_slot_props(g_scene, "lower");
    if (props == NULL || !lab14_json_int(props, "touch", 1)) {
        return;
    }
    set_json_int(props, "focused", index);
    remount_current();
}

static void pin_digit(lv_event_t *event)
{
    (void)event;
    cJSON *header = lab14_slot_props(g_scene, "upper");
    if (header == NULL) {
        return;
    }
    if (strcmp(lab14_json_str(header, "state", "partial"), "partial") != 0) {
        return;
    }
    int filled = lab14_json_int(header, "filled", 0);
    if (filled >= 4) {
        return;
    }
    set_json_int(header, "filled", filled + 1);
    remount_current();
}

static void format_pin_marks(char *out, size_t n, int filled)
{
    int count = filled;
    if (count < 0) {
        count = 0;
    }
    if (count > 4) {
        count = 4;
    }
    snprintf(out, n, "%c %c %c %c",
             count > 0 ? '*' : '_',
             count > 1 ? '*' : '_',
             count > 2 ? '*' : '_',
             count > 3 ? '*' : '_');
}

static uint32_t darker(uint32_t color)
{
    unsigned r = (color >> 16) & 0xFF;
    unsigned g = (color >> 8) & 0xFF;
    unsigned b = color & 0xFF;
    r = r > 24 ? r - 24 : 0;
    g = g > 24 ? g - 24 : 0;
    b = b > 24 ? b - 24 : 0;
    return (r << 16) | (g << 8) | b;
}

static void initials_from_name(const char *name, char *out, size_t n)
{
    size_t i = 0;
    if (name == NULL || name[0] == '\0') {
        snprintf(out, n, "?");
        return;
    }
    out[i++] = name[0];
    const char *space = strchr(name, ' ');
    if (space != NULL && space[1] != '\0' && i + 1 < n) {
        out[i++] = space[1];
    }
    out[i] = '\0';
}

static void mount_user_card(lv_obj_t *parent, cJSON *user, int x, int y, int w, int h, bool focused, bool touch, int index)
{
    const lab14_tokens_t *t = &g_catalog->tokens;
    const char *name = lab14_json_str(user, "name", "");
    uint32_t bg = lab14_hex(lab14_json_str(user, "background", NULL), t->card_peek);
    uint32_t text = lab14_hex(lab14_json_str(user, "text", NULL), t->text_primary);
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_scrollable(card, false);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, t->card_radius, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(darker(bg)), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_opa(card, focused ? LV_OPA_COVER : (lv_opa_t)t->unfocused_opa, 0);

    int avatar = t->avatar * w / t->card_w;
    lv_obj_t *face = lv_obj_create(card);
    lv_obj_set_size(face, avatar, avatar);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(face, 0, 0);
    lv_obj_set_style_bg_color(face, lv_color_hex(0xF4F4F4), 0);
    lv_obj_set_style_pad_all(face, 0, 0);
    lv_obj_set_scrollable(face, false);
    lv_obj_align(face, LV_ALIGN_TOP_MID, 0, 12 * w / t->card_w);
    char initials[4];
    initials_from_name(name, initials, sizeof(initials));
    lv_obj_t *initials_label = lv_label_create(face);
    lv_label_set_text(initials_label, initials);
    lv_obj_center(initials_label);
    style_label(initials_label, 0x101418, t->type16);

    lv_obj_t *name_label = lv_label_create(card);
    lv_label_set_text(name_label, name);
    lv_obj_set_width(name_label, w - 8);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(name_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    style_label(name_label, text, t->type16);

    if (touch) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, focus_card, LV_EVENT_CLICKED, (void *)(intptr_t)index);
    }
}

static void panel_header_title_left(lv_obj_t *slot, cJSON *props)
{
    const lab14_tokens_t *t = &g_catalog->tokens;
    lv_obj_t *label = lv_label_create(slot);
    lv_label_set_text(label, lab14_json_str(props, "title", ""));
    lv_obj_set_pos(label, t->margin, 0);
    lv_obj_set_width(label, t->canvas_w - (2 * t->margin));
    lv_obj_set_height(label, t->split_y - (2 * t->margin));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    style_label(label, t->text_primary, t->type24);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, t->margin, 0);
}

static void panel_title_subtitle(lv_obj_t *slot, cJSON *props, bool with_action)
{
    const lab14_tokens_t *t = &g_catalog->tokens;
    int title_w = with_action ? t->act_title_w : t->ts_text_w;
    int sub_w = with_action ? t->act_sub_w : t->ts_text_w;
    lv_obj_t *title = lv_label_create(slot);
    lv_label_set_text(title, lab14_json_str(props, "title", ""));
    lv_obj_set_pos(title, t->ts_title_x, t->ts_title_y);
    lv_obj_set_width(title, title_w);
    lv_obj_set_height(title, t->type24);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    style_label(title, t->text_primary, t->type24);

    lv_obj_t *subtitle = lv_label_create(slot);
    lv_label_set_text(subtitle, lab14_json_str(props, "subtitle", ""));
    lv_obj_set_pos(subtitle, t->ts_sub_x, t->ts_sub_y);
    lv_obj_set_width(subtitle, sub_w);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
    style_label(subtitle, t->text_secondary, t->type16);

    if (with_action) {
        make_button(slot, lab14_json_str(props, "action", "Back"),
                    t->act_x, t->act_y, t->act_w, t->act_h, t->type16, tap_notice, "header-action");
    }
}

static void panel_pin_status(lv_obj_t *slot, cJSON *props)
{
    const lab14_tokens_t *t = &g_catalog->tokens;
    const char *state = lab14_json_str(props, "state", "partial");
    int filled = lab14_json_int(props, "filled", 0);
    const char *name = lab14_json_str(props, "name", "");

    if (strcmp(state, "wrong") == 0) {
        lv_obj_t *wrong = lv_label_create(slot);
        lv_label_set_text(wrong, "wrong pin");
        style_label(wrong, t->text_primary, t->type24);
        lv_obj_align(wrong, LV_ALIGN_LEFT_MID, t->margin, 0);
        return;
    }

    lv_obj_t *prefix = lv_label_create(slot);
    lv_label_set_text(prefix, t->pin_prefix);
    style_label(prefix, t->text_primary, t->type16);
    lv_obj_align(prefix, LV_ALIGN_LEFT_MID, t->margin, 0);

    lv_obj_t *face = lv_obj_create(slot);
    lv_obj_set_size(face, t->pin_avatar, t->pin_avatar);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(face, 0, 0);
    lv_obj_set_style_bg_color(face, lv_color_hex(0x5AA0E8), 0);
    lv_obj_set_style_pad_all(face, 0, 0);
    lv_obj_set_scrollable(face, false);
    lv_obj_align_to(face, prefix, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_t *initial = lv_label_create(face);
    char initials[4];
    initials_from_name(name, initials, sizeof(initials));
    lv_label_set_text(initial, initials);
    lv_obj_center(initial);
    style_label(initial, 0x101418, t->type16);

    lv_obj_t *name_label = lv_label_create(slot);
    lv_label_set_text(name_label, name);
    lv_obj_set_width(name_label, 88);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
    style_label(name_label, t->text_primary, t->type24);
    lv_obj_align_to(name_label, face, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    char marks[24];
    lv_obj_t *marks_label = lv_label_create(slot);
    if (strcmp(state, "checking") == 0) {
        lv_label_set_text(marks_label, "checking...");
        style_label(marks_label, t->text_secondary, t->type16);
    } else {
        format_pin_marks(marks, sizeof(marks), filled);
        lv_label_set_text(marks_label, marks);
        style_label(marks_label, filled > 0 ? t->accent : t->text_secondary, t->type32);
        if (filled < 4) {
            /* Mixed * / _ colors: draw four glyphs instead when needed. */
            lv_obj_set_style_text_color(marks_label, lv_color_hex(t->accent), 0);
        }
        if (filled == 0) {
            lv_obj_set_style_text_color(marks_label, lv_color_hex(t->text_secondary), 0);
        }
    }
    lv_obj_align(marks_label, LV_ALIGN_RIGHT_MID, -t->margin, 0);
}

static void panel_roster(lv_obj_t *slot, cJSON *props)
{
    const lab14_tokens_t *t = &g_catalog->tokens;
    cJSON *users = cJSON_GetObjectItemCaseSensitive(props, "users");
    if (!cJSON_IsArray(users)) {
        return;
    }
    int count = cJSON_GetArraySize(users);
    int focused = lab14_json_int(props, "focused", 0);
    bool touch = lab14_json_int(props, "touch", 1) != 0;
    if (count < 1) {
        return;
    }
    if (focused < 0) {
        focused = 0;
    }
    if (focused >= count) {
        focused = count - 1;
    }

    for (int i = 0; i < count; i++) {
        bool is_focused = i == focused;
        int scale = is_focused ? 100 : t->unfocused_scale_pct;
        int w = t->card_w * scale / 100;
        int h = t->card_h * scale / 100;
        int center = t->canvas_w / 2;
        int x = center - (w / 2) + ((i - focused) * (t->card_w + t->card_gap));
        int y = (t->canvas_h - t->split_y - h) / 2;
        mount_user_card(slot, cJSON_GetArrayItem(users, i), x, y, w, h, is_focused, touch, i);
    }
}

static void panel_four_button(lv_obj_t *slot, cJSON *props)
{
    const lab14_tokens_t *t = &g_catalog->tokens;
    cJSON *buttons = cJSON_GetObjectItemCaseSensitive(props, "buttons");
    for (int i = 0; i < 4; i++) {
        const char *name = "";
        if (cJSON_IsArray(buttons) && i < cJSON_GetArraySize(buttons)) {
            cJSON *item = cJSON_GetArrayItem(buttons, i);
            if (cJSON_IsString(item)) {
                name = item->valuestring;
            }
        }
        int col = i % 2;
        int row = i / 2;
        int x = t->four_gutter + (col * (t->four_w + t->four_gutter));
        int y = t->four_gutter + (row * (t->four_h + t->four_gutter));
        make_button(slot, name, x, y, t->four_w, t->four_h, t->type16, tap_notice, "four-button");
    }
}

static void panel_two_button(lv_obj_t *slot, cJSON *props)
{
    const lab14_tokens_t *t = &g_catalog->tokens;
    lv_obj_t *label = lv_label_create(slot);
    lv_label_set_text(label, lab14_json_str(props, "label", ""));
    lv_obj_set_pos(label, t->margin, t->two_label_y);
    lv_obj_set_width(label, t->two_btn_w);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    style_label(label, t->text_primary, t->type16);

    cJSON *buttons = cJSON_GetObjectItemCaseSensitive(props, "buttons");
    const char *top = "";
    const char *bottom = "";
    if (cJSON_IsArray(buttons) && cJSON_GetArraySize(buttons) > 0 && cJSON_IsString(cJSON_GetArrayItem(buttons, 0))) {
        top = cJSON_GetArrayItem(buttons, 0)->valuestring;
    }
    if (cJSON_IsArray(buttons) && cJSON_GetArraySize(buttons) > 1 && cJSON_IsString(cJSON_GetArrayItem(buttons, 1))) {
        bottom = cJSON_GetArrayItem(buttons, 1)->valuestring;
    }
    make_button(slot, top, t->margin, t->two_btn_y0, t->two_btn_w, t->two_btn_h, t->type16, tap_notice, "two-button");
    make_button(slot, bottom, t->margin, t->two_btn_y1, t->two_btn_w, t->two_btn_h, t->type16, tap_notice, "two-button");
}

static void panel_pin_grid(lv_obj_t *slot, cJSON *props)
{
    const lab14_tokens_t *t = &g_catalog->tokens;
    const char *state = lab14_json_str(props, "state", "ready");
    bool checking = strcmp(state, "checking") == 0;
    static const char *digits[9] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
    for (int i = 0; i < 9; i++) {
        int col = i % 3;
        int row = i / 3;
        int x = t->pin_pad + (col * (t->pin_w + t->pin_gap));
        int y = t->pin_pad + (row * (t->pin_h + t->pin_gap));
        lv_event_cb_t cb = checking ? NULL : pin_digit;
        lv_obj_t *key = make_button(slot, digits[i], x, y, t->pin_w, t->pin_h, t->type32, cb, NULL);
        lv_obj_t *label = lv_obj_get_child(key, 0);
        if (label != NULL) {
            style_label(label, t->text_primary, t->type32);
        }
        if (checking) {
            lv_obj_set_style_opa(key, (lv_opa_t)t->pin_dim_opa, 0);
            lv_obj_add_state(key, LV_STATE_DISABLED);
        }
    }
}

static void panel_status(lv_obj_t *slot, cJSON *props)
{
    const lab14_tokens_t *t = &g_catalog->tokens;
    const char *variant = lab14_json_str(props, "variant", "waiting");
    const char *headline = lab14_json_str(props, "headline", "connecting");
    lv_obj_t *box = lv_obj_create(slot);
    lv_obj_set_size(box, t->canvas_w - (2 * t->margin), t->canvas_h - t->split_y - (2 * t->margin));
    lv_obj_align(box, LV_ALIGN_CENTER, 0, 0);
    paint_plain(box, t->surface_lower);

    lv_obj_t *line = lv_label_create(box);
    lv_label_set_text(line, headline);
    style_label(line, t->text_primary, t->type32);
    lv_obj_align(line, LV_ALIGN_CENTER, 0, strcmp(variant, "waiting") == 0 ? -16 : 0);

    if (strcmp(variant, "waiting") == 0) {
        g_dots_label = lv_label_create(box);
        lv_label_set_text(g_dots_label, ".  ");
        style_label(g_dots_label, t->text_secondary, t->type24);
        lv_obj_align(g_dots_label, LV_ALIGN_CENTER, 0, 20);
        g_dots_timer = lv_timer_create(dots_cb, 400, NULL);
    }
}

static void mount_panel(lv_obj_t *slot, const char *panel, cJSON *props)
{
    if (panel == NULL || strcmp(panel, "none") == 0) {
        return;
    }
    if (strcmp(panel, "PANEL-HEADER-TITLE-LEFT") == 0) {
        panel_header_title_left(slot, props);
    } else if (strcmp(panel, "PANEL-HEADER-TITLE-SUBTITLE") == 0) {
        panel_title_subtitle(slot, props, false);
    } else if (strcmp(panel, "PANEL-HEADER-TITLE-SUBTITLE-WITH-ACTION") == 0) {
        panel_title_subtitle(slot, props, true);
    } else if (strcmp(panel, "PANEL-HEADER-PIN-STATUS") == 0) {
        panel_pin_status(slot, props);
    } else if (strcmp(panel, "PANEL-BODY-ROSTER-CAROUSEL") == 0) {
        panel_roster(slot, props);
    } else if (strcmp(panel, "PANEL-BODY-FOUR-BUTTON") == 0) {
        panel_four_button(slot, props);
    } else if (strcmp(panel, "PANEL-BODY-LABEL-WITH-TWO-BUTTONS") == 0) {
        panel_two_button(slot, props);
    } else if (strcmp(panel, "PANEL-BODY-PIN-GRID") == 0) {
        panel_pin_grid(slot, props);
    } else if (strcmp(panel, "PANEL-STATUS-MESSAGE") == 0) {
        panel_status(slot, props);
    }
}

static void long_press_back(lv_event_t *event)
{
    (void)event;
    if (!g_showing_index) {
        lab14_ui_show_index();
    }
}

static void choose_scene(lv_event_t *event)
{
    const char *id = (const char *)lv_event_get_user_data(event);
    lab14_ui_show_scene(id);
}

static void mount_full_canvas(const lab14_scene_t *scene)
{
    const lab14_tokens_t *t = &g_catalog->tokens;
    paint_plain(g_root, t->surface);
    lv_obj_t *label = lv_label_create(g_root);
    lv_label_set_text(label, lab14_json_str(scene->props, "message", ""));
    lv_obj_set_width(label, t->canvas_w - (2 * t->margin));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    style_label(label, t->text_primary, t->type24);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

static void mount_split(const lab14_scene_t *scene)
{
    const lab14_tokens_t *t = &g_catalog->tokens;
    paint_plain(g_root, t->surface);
    lv_obj_t *upper = make_slot(g_root, 0, 0, t->canvas_w, t->split_y, t->surface_upper);
    lv_obj_t *lower = make_slot(g_root, 0, t->split_y, t->canvas_w, t->canvas_h - t->split_y, t->surface_lower);
    mount_panel(upper, lab14_slot_panel(scene, "upper"), lab14_slot_props(scene, "upper"));
    mount_panel(lower, lab14_slot_panel(scene, "lower"), lab14_slot_props(scene, "lower"));
    lv_obj_add_event_cb(upper, long_press_back, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(lower, long_press_back, LV_EVENT_LONG_PRESSED, NULL);
}

static void mount_scene_id_badge(const lab14_scene_t *scene)
{
    const lab14_tokens_t *t = &g_catalog->tokens;
    lv_obj_t *badge = lv_obj_create(g_root);
    lv_obj_set_size(badge, 72, 22);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_color(badge, lv_color_hex(t->card_peek), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(badge, 4, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_set_scrollable(badge, false);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *label = lv_label_create(badge);
    lv_label_set_text(label, scene->id != NULL ? scene->id : "");
    style_label(label, t->text_primary, t->type16);
    lv_obj_center(label);
}

static void make_index_row(lv_obj_t *parent, const lab14_scene_t *scene, int y)
{
    const lab14_tokens_t *t = &g_catalog->tokens;
    int width = t->canvas_w - (2 * t->margin);
    char text[80];
    snprintf(text, sizeof(text), "%s  %s", scene->id != NULL ? scene->id : "?",
             scene->title != NULL ? scene->title : "");
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, t->margin, y);
    lv_obj_set_size(button, width, 48);
    lv_obj_set_style_bg_color(button, lv_color_hex(t->control), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(t->key_pressed), LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, t->card_radius, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_pad_hor(button, 8, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, width - 16);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    style_label(label, t->text_primary, t->type16);
    lv_obj_add_event_cb(button, choose_scene, LV_EVENT_CLICKED, (void *)scene->id);
}

static void emit_scene_trace(const lab14_scene_t *scene)
{
    printf("LAB14_TRACE {\"scene\":\"%s\",\"screen\":\"%s\",\"state\":\"%s\",\"layout\":\"%s\",\"upper\":\"%s\",\"lower\":\"%s\"}\n",
           scene->id,
           scene->screen != NULL ? scene->screen : "",
           scene->state != NULL ? scene->state : "",
           scene->layout,
           lab14_slot_panel(scene, "upper"),
           lab14_slot_panel(scene, "lower"));
}

bool lab14_ui_show_scene(const char *id)
{
    const lab14_scene_t *scene = lab14_catalog_get(g_catalog, id);
    if (scene == NULL) {
        printf("LAB14_SCENE_MISSING id=%s\n", id != NULL ? id : "");
        return false;
    }
    stop_dots();
    lv_obj_clean(g_root);
    g_scene = scene;
    g_showing_index = false;
    snprintf(g_current_id, sizeof(g_current_id), "%s", scene->id);
    if (strcmp(scene->layout, "LAYOUT-FULL-CANVAS") == 0) {
        mount_full_canvas(scene);
    } else {
        mount_split(scene);
    }
    mount_scene_id_badge(scene);
    lv_obj_add_event_cb(g_root, long_press_back, LV_EVENT_LONG_PRESSED, NULL);
    printf("LAB14_SCENE id=%s screen=%s\n", scene->id, scene->screen != NULL ? scene->screen : "");
    return true;
}

void lab14_ui_handle_boot(void)
{
    if (g_showing_index) {
        return;
    }
    printf("LAB14_BOOT action=index scene=%s\n", g_current_id);
    lab14_ui_show_index();
}

void lab14_ui_show_index(void)
{
    const lab14_tokens_t *t = &g_catalog->tokens;
    stop_dots();
    lv_obj_clean(g_root);
    g_scene = NULL;
    g_showing_index = true;
    g_current_id[0] = '\0';
    paint_plain(g_root, t->surface);

    lv_obj_t *title = lv_label_create(g_root);
    lv_label_set_text(title, g_catalog->title);
    lv_obj_set_pos(title, t->margin, 8);
    style_label(title, t->text_primary, t->type24);

    lv_obj_t *hint = lv_label_create(g_root);
    lv_label_set_text(hint, "Tap a scene. BOOT returns here.");
    lv_obj_set_pos(hint, t->margin, 36);
    lv_obj_set_width(hint, t->canvas_w - (2 * t->margin));
    style_label(hint, t->text_secondary, t->type16);

    lv_obj_t *list = lv_obj_create(g_root);
    lv_obj_set_pos(list, 0, 60);
    lv_obj_set_size(list, t->canvas_w, t->canvas_h - 60);
    paint_plain(list, t->surface);
    lv_obj_set_scrollable(list, true);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    int y = 0;
    for (int i = 0; i < g_catalog->scene_count; i++) {
        make_index_row(list, &g_catalog->scenes[i], y);
        y += 56;
    }
    lv_obj_t *spacer = lv_obj_create(list);
    lv_obj_set_pos(spacer, 0, y);
    lv_obj_set_size(spacer, 1, 8);
    paint_plain(spacer, t->surface);
    printf("LAB14_INDEX scenes=%d\n", g_catalog->scene_count);
}

bool lab14_ui_show_offset(int delta)
{
    if (g_catalog->scene_count < 1) {
        return false;
    }
    int index = 0;
    if (!g_showing_index && g_current_id[0] != '\0') {
        index = lab14_catalog_index_of(g_catalog, g_current_id);
        if (index < 0) {
            index = 0;
        }
        index += delta;
    }
    if (index < 0) {
        index = g_catalog->scene_count - 1;
    }
    if (index >= g_catalog->scene_count) {
        index = 0;
    }
    return lab14_ui_show_scene(g_catalog->scenes[index].id);
}

static void trim_line(char *line)
{
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) {
        line[--n] = '\0';
    }
}

bool lab14_ui_handle_line(const char *raw)
{
    char line[192];
    snprintf(line, sizeof(line), "%s", raw != NULL ? raw : "");
    trim_line(line);
    if (line[0] == '\0') {
        return false;
    }

    const char *command = line;
    if (strncmp(line, "LAB14_", 6) == 0) {
        command = line + 6;
    }

    if (strcmp(command, "index") == 0 || strcmp(command, "INDEX") == 0) {
        lab14_ui_show_index();
        return true;
    }
    if (strcmp(command, "next") == 0 || strcmp(command, "NEXT") == 0) {
        return lab14_ui_show_offset(1);
    }
    if (strcmp(command, "prev") == 0 || strcmp(command, "PREV") == 0) {
        return lab14_ui_show_offset(-1);
    }
    if (strcmp(command, "list") == 0 || strcmp(command, "LIST") == 0) {
        for (int i = 0; i < g_catalog->scene_count; i++) {
            const lab14_scene_t *scene = &g_catalog->scenes[i];
            printf("LAB14_SCENE_ID %s title=%s\n", scene->id,
                   scene->title != NULL ? scene->title : "");
        }
        return true;
    }
    if (strncmp(command, "scene ", 6) == 0 || strncmp(command, "SCENE ", 6) == 0) {
        return lab14_ui_show_scene(command + 6);
    }
    return false;
}

void lab14_run_harness(void)
{
    for (int i = 0; i < g_catalog->scene_count; i++) {
        if (lab14_ui_show_scene(g_catalog->scenes[i].id)) {
            emit_scene_trace(&g_catalog->scenes[i]);
        }
    }
    lab14_ui_show_index();
    printf("LAB14_HARNESS_DONE status=complete scenes=%d\n", g_catalog->scene_count);
}

bool lab14_ui_init(lv_obj_t *root, lab14_catalog_t *catalog, bool scripted)
{
    if (root == NULL || catalog == NULL || catalog->scene_count < 1) {
        return false;
    }
    g_root = root;
    g_catalog = catalog;
    lv_obj_set_size(root, catalog->tokens.canvas_w, catalog->tokens.canvas_h);
    lab14_ui_show_index();
    (void)scripted;
    return true;
}
