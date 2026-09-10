/*
 * p13 — LVGL UI showcase for ESP32-S3 BOX-3 (320×240).
 *
 * Multi-page demo covering shapes, text, icons/images, colors, scrolling,
 * and layered panels. Each page is numbered and titled for feedback.
 *
 * Navigation:
 *   Boot (GPIO0) or red circle → next demo page (wraps 27 → 1)
 *   On-screen "Menu" button → main menu (scrollable index)
 *   Menu rows → jump to a page
 *
 * No server or Wi-Fi. -- PASS p13 after one next-page via boot or circle.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"

#include "board.h"
#include "pass.h"

static const char *TAG = "p13";

/* ── Display geometry ─────────────────────────────────────────────── */
#define SCR_W          320
#define SCR_H          240
#define CHROME_H       36
#define CONTENT_Y      CHROME_H
#define CONTENT_H      (SCR_H - CHROME_H - 4)
#define PAD            8
#define MENU_ROW_H     36
#define ROW_H          56
#define ROW_GAP        8
#define LIST_ROW_H     72
#define CHROME_ROW_H   56

/* ── Color tokens ─────────────────────────────────────────────────── */
#define COL_BG         0x101418
#define COL_PANEL      0x1E242C
#define COL_ACCENT     0x5AA0E8
#define COL_TEXT       0xE8F0E8
#define COL_MUTED      0x8A9298
#define COL_WARN       0xE8C040
#define COL_OK         0x7DCC7A
#define COL_HOT        0xE85A5A

/* ── Page indices ─────────────────────────────────────────────────── */
enum {
    PAGE_MENU = 0,
    /* Set 1 — shapes */
    PAGE_RECT = 1,
    PAGE_ROUND,
    PAGE_TRI,
    PAGE_CIRC,
    PAGE_COMP,
    PAGE_POS,
    /* Set 2 — text */
    PAGE_FONTS,
    PAGE_ALIGN_H,
    PAGE_ALIGN_V,
    PAGE_WRAP,
    PAGE_TEXT_IN_SHAPE,
    /* Set 3 — icons & images */
    PAGE_ICON_SIZES,
    PAGE_IMG_POS,
    PAGE_IMG_SCALE,
    /* Set 4 — colors (display id 15 removed) */
    PAGE_PAL_LIGHT,
    PAGE_PAL_DARK,
    PAGE_PAL_ACCENT,
    PAGE_GRADIENT,
    PAGE_THEMES,
    /* Set 5 — scroll */
    PAGE_SCROLL_V,
    PAGE_SCROLL_H = 22,
    PAGE_SCROLL_LIST = 23,
    PAGE_SCROLL_CHROME = 24,
    PAGE_OVERLAP = 25,
    PAGE_ZINDEX = 26,
    PAGE_MODAL = 27,
    PAGE_SCROLL_H_TALL = 28,
    PAGE_SCROLL_H_WIDE = 29,
    PAGE_SCROLL_H_SNAP = 30,
    PAGE_SCROLL_H_SNAP_GRAD = 31,
    PAGE_MODAL_SCROLL = 32,
    PAGE_SCROLL_H_SNAP_GRAD_OPT = 33,
    PAGE_COUNT
};

#define PAGE_TABLE_COUNT  32
#define DEMO_FIRST        PAGE_RECT

typedef void (*page_builder_t)(lv_obj_t *scr);

typedef struct {
    int page;
    int id;
    const char *title;
    page_builder_t build;
} page_info_t;

static const page_info_t s_page_table[];

static lv_obj_t *s_screens[PAGE_COUNT];
static lv_obj_t *s_content[PAGE_COUNT];
static int s_cur = -1;
static bool s_passed;
static bool s_saw_next;
static bool s_sample_ready;
static int64_t s_last_nav_us;

#define NAV_DEBOUNCE_US  500000

/* Sample RGB565 image (48×48 gradient blocks) for image demos. */
#define IMG_W  48
#define IMG_H  48
static uint16_t s_img_px[IMG_W * IMG_H];
static lv_image_dsc_t s_sample_img;

/* Triangle row canvases (one bitmap per rotation row on page 03). */
#define TRI_W       80
#define TRI_H       70
#define TRI_PIX     (TRI_W * TRI_H)
#define TRI_ROW_W   296
#define TRI_ROW_H   88
#define TRI_ROW_PIX (TRI_ROW_W * TRI_ROW_H)
#define TRI_ROW_N   4
static uint16_t *s_tri_row_fb[TRI_ROW_N];
static uint16_t s_comp_tri_fb[TRI_PIX];

static uint16_t *tri_row_buf(int row_idx)
{
    if (row_idx < 0 || row_idx >= TRI_ROW_N) {
        return NULL;
    }
    if (s_tri_row_fb[row_idx] != NULL) {
        return s_tri_row_fb[row_idx];
    }
    s_tri_row_fb[row_idx] =
        heap_caps_malloc(TRI_ROW_PIX * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_tri_row_fb[row_idx] == NULL) {
        s_tri_row_fb[row_idx] =
            heap_caps_malloc(TRI_ROW_PIX * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return s_tri_row_fb[row_idx];
}

/* ── LVGL version helpers ─────────────────────────────────────────── */
#if LVGL_VERSION_MAJOR >= 9
#define BTN_CREATE(parent)  lv_button_create(parent)
#define SCR_LOAD(scr)       lv_screen_load(scr)
#define SCR_ACTIVE()        lv_screen_active()
#else
#define BTN_CREATE(parent)  lv_btn_create(parent)
#define SCR_LOAD(scr)       lv_scr_load(scr)
#define SCR_ACTIVE()        lv_disp_get_scr_act(NULL)
#endif

/* ── Font helpers (CONFIG_LV_FONT_MONTSERRAT_* in sdkconfig.defaults) ── */
static const lv_font_t *montserrat_for_px(int px)
{
    switch (px) {
#if CONFIG_LV_FONT_MONTSERRAT_14
    case 14:
        return &lv_font_montserrat_14;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_16
    case 16:
        return &lv_font_montserrat_16;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_18
    case 18:
        return &lv_font_montserrat_18;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_20
    case 20:
        return &lv_font_montserrat_20;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_22
    case 22:
        return &lv_font_montserrat_22;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_24
    case 24:
        return &lv_font_montserrat_24;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_26
    case 26:
        return &lv_font_montserrat_26;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_28
    case 28:
        return &lv_font_montserrat_28;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_30
    case 30:
        return &lv_font_montserrat_30;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_32
    case 32:
        return &lv_font_montserrat_32;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_34
    case 34:
        return &lv_font_montserrat_34;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_36
    case 36:
        return &lv_font_montserrat_36;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_38
    case 38:
        return &lv_font_montserrat_38;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_40
    case 40:
        return &lv_font_montserrat_40;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_42
    case 42:
        return &lv_font_montserrat_42;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_44
    case 44:
        return &lv_font_montserrat_44;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_46
    case 46:
        return &lv_font_montserrat_46;
#endif
#if CONFIG_LV_FONT_MONTSERRAT_48
    case 48:
        return &lv_font_montserrat_48;
#endif
    default:
        return NULL;
    }
}

static const lv_font_t *font_medium(void)
{
    const lv_font_t *font = montserrat_for_px(28);
    return font ? font : LV_FONT_DEFAULT;
}

/* ── Low-level drawing helpers ────────────────────────────────────── */
static uint16_t rgb565(uint32_t hex)
{
    unsigned r = (hex >> 16) & 0xFFu;
    unsigned g = (hex >> 8) & 0xFFu;
    unsigned b = hex & 0xFFu;
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

static void init_sample_image(void)
{
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            int block = ((x / 16) + (y / 16)) & 1;
            uint32_t hex = block ? 0x5AA0E8 : 0xE8C040;
            s_img_px[y * IMG_W + x] = rgb565(hex);
        }
    }
    s_sample_img.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_sample_img.header.cf = LV_COLOR_FORMAT_RGB565;
    s_sample_img.header.w = IMG_W;
    s_sample_img.header.h = IMG_H;
    s_sample_img.header.stride = IMG_W * 2;
    s_sample_img.data_size = sizeof(s_img_px);
    s_sample_img.data = (const uint8_t *)s_img_px;
}

static int edge_fn(int ax, int ay, int bx, int by, int cx, int cy)
{
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

static void tri_fill_fb(uint16_t *fb, int fw, int fh, int x0, int y0, int x1, int y1,
                        int x2, int y2, uint16_t col, bool outline, uint16_t line_col)
{
    int min_y = y0;
    if (y1 < min_y) {
        min_y = y1;
    }
    if (y2 < min_y) {
        min_y = y2;
    }
    int max_y = y0;
    if (y1 > max_y) {
        max_y = y1;
    }
    if (y2 > max_y) {
        max_y = y2;
    }
    if (min_y < 0) {
        min_y = 0;
    }
    if (max_y >= fh) {
        max_y = fh - 1;
    }

    for (int y = min_y; y <= max_y; y++) {
        for (int x = 0; x < fw; x++) {
            int w0 = edge_fn(x1, y1, x2, y2, x, y);
            int w1 = edge_fn(x2, y2, x0, y0, x, y);
            int w2 = edge_fn(x0, y0, x1, y1, x, y);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                fb[y * fw + x] = col;
            }
        }
    }

    if (!outline) {
        return;
    }
    /* Re-stamp border pixels by checking distance to edges (3 px). */
    for (int y = min_y; y <= max_y; y++) {
        for (int x = 0; x < fw; x++) {
            int w0 = edge_fn(x1, y1, x2, y2, x, y);
            int w1 = edge_fn(x2, y2, x0, y0, x, y);
            int w2 = edge_fn(x0, y0, x1, y1, x, y);
            bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (!inside) {
                continue;
            }
            int m0 = w0 < 0 ? -w0 : w0;
            int m1 = w1 < 0 ? -w1 : w1;
            int m2 = w2 < 0 ? -w2 : w2;
            int m = m0;
            if (m1 < m) {
                m = m1;
            }
            if (m2 < m) {
                m = m2;
            }
            if (m < 900) {
                fb[y * fw + x] = line_col;
            }
        }
    }
}

static void clear_fb(uint16_t *fb, int n, uint16_t c)
{
    for (int i = 0; i < n; i++) {
        fb[i] = c;
    }
}

static lv_obj_t *plain_box(lv_obj_t *parent, int x, int y, int w, int h, int radius,
                           uint32_t fill, uint32_t border, int border_w, lv_opa_t fill_opa)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_opa(o, fill_opa, 0);
    if (border_w > 0) {
        lv_obj_set_style_border_width(o, border_w, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
        lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    }
    return o;
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text, int w, int h)
{
    lv_obj_t *btn = BTN_CREATE(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, text);
    lv_obj_set_style_text_color(lab, lv_color_hex(COL_TEXT), 0);
    lv_obj_center(lab);
    return btn;
}

static void style_screen(lv_obj_t *scr, uint32_t bg)
{
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
}

static void add_content_area(lv_obj_t *scr, int page_idx, uint32_t bg)
{
    lv_obj_t *area = lv_obj_create(scr);
    lv_obj_remove_style_all(area);
    lv_obj_set_pos(area, 0, CONTENT_Y);
    lv_obj_set_size(area, SCR_W, CONTENT_H);
    lv_obj_set_style_bg_color(area, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(area, LV_OPA_COVER, 0);
    lv_obj_remove_flag(area, LV_OBJ_FLAG_SCROLLABLE);
    s_content[page_idx] = area;
}

static lv_obj_t *content_of(lv_obj_t *scr)
{
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (s_screens[i] == scr) {
            return s_content[i];
        }
    }
    return NULL;
}

static const page_info_t *page_info_for(int idx);
static void ensure_page(int idx);
static void drop_demo_screen(int idx);
static void go_page(int idx);

static void on_menu_back(lv_event_t *e)
{
    (void)e;
    if (!board_lvgl_lock(100)) {
        return;
    }
    go_page(PAGE_MENU);
    board_lvgl_unlock();
}

static void go_page(int idx)
{
    int prev;

    if (idx < PAGE_MENU || idx >= PAGE_COUNT) {
        return;
    }
    if (s_cur >= 0 && idx == s_cur) {
        return;
    }
    if (idx != PAGE_MENU) {
        ensure_page(idx);
    }
    if (s_screens[idx] == NULL) {
        ESP_LOGE(TAG, "page %d missing (heap %u)", idx, (unsigned)esp_get_free_heap_size());
        return;
    }
    prev = s_cur;
    s_cur = idx;
    /* Load the new screen before deleting the old one — LVGL must not
     * lv_obj_delete() the screen that is still active (white flash / crash). */
    SCR_LOAD(s_screens[idx]);
    if (prev > PAGE_MENU && prev != idx) {
        drop_demo_screen(prev);
    }
    ESP_LOGI(TAG, "page %02d heap=%u", idx, (unsigned)esp_get_free_heap_size());
}

static void on_menu_pick(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (!board_lvgl_lock(100)) {
        return;
    }
    go_page(idx);
    board_lvgl_unlock();
}

static void nav_next(void)
{
    int next = s_cur;
    int64_t now = esp_timer_get_time();

    if (now - s_last_nav_us < NAV_DEBOUNCE_US) {
        return;
    }
    s_last_nav_us = now;

    next = DEMO_FIRST;
    if (s_cur > PAGE_MENU) {
        for (size_t i = 0; i < PAGE_TABLE_COUNT; i++) {
            if (s_page_table[i].page == s_cur) {
                if (i + 1 < PAGE_TABLE_COUNT) {
                    next = s_page_table[i + 1].page;
                }
                break;
            }
        }
    }
    if (!board_lvgl_lock(500)) {
        return;
    }
    go_page(next);
    board_lvgl_unlock();
    if (!s_passed) {
        s_saw_next = true;
        s_passed = true;
        demo_pass("p13");
    }
}

static void on_boot_btn(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    nav_next();
}

static void on_circle_btn(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    nav_next();
}

static void add_chrome(lv_obj_t *scr, int id, const char *title)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, SCR_W, CHROME_H);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    char num[48];
    snprintf(num, sizeof(num), "%02d  %s", id, title);
    lv_obj_t *head = lv_label_create(bar);
    lv_label_set_text(head, num);
    lv_obj_set_style_text_color(head, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(head, font_medium(), 0);
    lv_obj_set_pos(head, PAD, 4);

    lv_obj_t *menu = make_btn(scr, "Menu", 56, 24);
    lv_obj_align(menu, LV_ALIGN_TOP_RIGHT, -PAD, 6);
    lv_obj_add_event_cb(menu, on_menu_back, LV_EVENT_CLICKED, NULL);
}

static lv_obj_t *new_demo_screen(int page_idx, int id, const char *title, uint32_t bg)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    style_screen(scr, bg);
    add_chrome(scr, id, title);
    add_content_area(scr, page_idx, bg);
    return scr;
}

/* ── Page builders ────────────────────────────────────────────────── */

static void page_rectangles(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    /* Solid fills at (12,12), (120,12), (228,12) — 80×56 boxes. */
    plain_box(c, 12, 12, 80, 56, 0, COL_ACCENT, 0, 0, LV_OPA_COVER);
    plain_box(c, 120, 12, 80, 56, 0, COL_OK, 0, 0, LV_OPA_COVER);
    plain_box(c, 228, 12, 80, 56, 0, COL_WARN, 0, 0, LV_OPA_COVER);

    /* Outlined only: bg transparent, border 1/3/6 px at y=88. */
    plain_box(c, 12, 88, 80, 56, 0, 0, COL_TEXT, 1, LV_OPA_TRANSP);
    plain_box(c, 120, 88, 80, 56, 0, 0, COL_ACCENT, 3, LV_OPA_TRANSP);
    plain_box(c, 228, 88, 80, 56, 0, 0, COL_HOT, 6, LV_OPA_TRANSP);

    lv_obj_t *hint = lv_label_create(c);
    lv_label_set_text(hint, "top: solid fills\nmid: outline widths 1/3/6");
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_pos(hint, 12, 158);
}

static void page_rounded(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    /* Radius 4 / 12 / 24 on 88×64 boxes — shows corner rounding curve. */
    plain_box(c, 16, 16, 88, 64, 4, COL_PANEL, COL_ACCENT, 2, LV_OPA_COVER);
    plain_box(c, 116, 16, 88, 64, 12, COL_PANEL, COL_OK, 2, LV_OPA_COVER);
    plain_box(c, 216, 16, 88, 64, 24, COL_PANEL, COL_WARN, 2, LV_OPA_COVER);

    plain_box(c, 40, 100, 240, 56, 32, COL_ACCENT, 0, 0, LV_OPA_40);

    lv_obj_t *hint = lv_label_create(c);
    lv_label_set_text(hint, "radius 4 / 12 / 24 / 32 (wide pill)");
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_pos(hint, 16, 168);
}

static void rot_pt(int pivot_x, int pivot_y, int x, int y, int rot_tenths, int *ox, int *oy)
{
    int dx = x - pivot_x;
    int dy = y - pivot_y;

    switch (rot_tenths) {
    case 900:
        *ox = pivot_x + dy;
        *oy = pivot_y - dx;
        break;
    case 1800:
        *ox = pivot_x - dx;
        *oy = pivot_y - dy;
        break;
    case 2700:
        *ox = pivot_x - dy;
        *oy = pivot_y + dx;
        break;
    default:
        *ox = x;
        *oy = y;
        break;
    }
}

static void tri_at_rot(uint16_t *fb, int fw, int fh, int rot_tenths, int x0, int y0, int x1, int y1,
                       int x2, int y2, uint16_t col, bool outline, uint16_t line_col)
{
    int ax;
    int ay;
    int bx;
    int by;
    int cx;
    int cy;
    /* Rotate each triangle about its own centroid — not a shared row pivot. */
    int pivot_x = (x0 + x1 + x2) / 3;
    int pivot_y = (y0 + y1 + y2) / 3;

    rot_pt(pivot_x, pivot_y, x0, y0, rot_tenths, &ax, &ay);
    rot_pt(pivot_x, pivot_y, x1, y1, rot_tenths, &bx, &by);
    rot_pt(pivot_x, pivot_y, x2, y2, rot_tenths, &cx, &cy);
    tri_fill_fb(fb, fw, fh, ax, ay, bx, by, cx, cy, col, outline, line_col);
}

static void paint_tri_row(int row_idx, int rot_tenths)
{
    uint16_t *fb = tri_row_buf(row_idx);

    if (fb == NULL) {
        return;
    }
    clear_fb(fb, TRI_ROW_PIX, rgb565(COL_BG));

    /* Upright template: three filled + three outline triangles. */
    tri_at_rot(fb, TRI_ROW_W, TRI_ROW_H, rot_tenths, 48, 8, 28, 42, 68, 42, rgb565(COL_ACCENT),
               false, 0);
    tri_at_rot(fb, TRI_ROW_W, TRI_ROW_H, rot_tenths, 128, 8, 108, 42, 148, 42, rgb565(COL_OK),
               false, 0);
    tri_at_rot(fb, TRI_ROW_W, TRI_ROW_H, rot_tenths, 208, 8, 188, 42, 228, 42, rgb565(COL_WARN),
               false, 0);
    tri_at_rot(fb, TRI_ROW_W, TRI_ROW_H, rot_tenths, 48, 46, 28, 80, 68, 80, rgb565(COL_PANEL),
               true, rgb565(COL_TEXT));
    tri_at_rot(fb, TRI_ROW_W, TRI_ROW_H, rot_tenths, 128, 46, 108, 80, 148, 80, rgb565(COL_PANEL),
               true, rgb565(COL_ACCENT));
    tri_at_rot(fb, TRI_ROW_W, TRI_ROW_H, rot_tenths, 208, 46, 188, 80, 228, 80, rgb565(COL_PANEL),
               true, rgb565(COL_HOT));
}

static void page_triangles(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    static const int rots[] = {0, 900, 1800, 2700};
    static const char *labels[] = {"0 deg  filled + outline", "90 deg", "180 deg", "270 deg"};
    const int row_step = TRI_ROW_H + 28;

    lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_AUTO);

    for (int r = 0; r < TRI_ROW_N; r++) {
        int y = 8 + r * row_step;
        uint16_t *fb = tri_row_buf(r);

        paint_tri_row(r, rots[r]);
        if (fb == NULL) {
            continue;
        }

        lv_obj_t *can = lv_canvas_create(c);
        lv_canvas_set_buffer(can, fb, TRI_ROW_W, TRI_ROW_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(can, 12, y);
        lv_obj_remove_flag(can, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(can, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *lab = lv_label_create(c);
        lv_label_set_text(lab, labels[r]);
        lv_obj_set_style_text_color(lab, lv_color_hex(COL_MUTED), 0);
        lv_obj_set_pos(lab, 16, y + TRI_ROW_H + 4);
    }

    lv_obj_t *end = lv_obj_create(c);
    lv_obj_remove_style_all(end);
    lv_obj_set_size(end, 1, 8);
    lv_obj_set_pos(end, 0, 8 + TRI_ROW_N * row_step);
}

static void page_circles(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    /* Circle = equal w/h + LV_RADIUS_CIRCLE. */
    plain_box(c, 24, 20, 64, 64, LV_RADIUS_CIRCLE, COL_ACCENT, 0, 0, LV_OPA_COVER);
    plain_box(c, 128, 20, 64, 64, LV_RADIUS_CIRCLE, 0, COL_OK, 4, LV_OPA_TRANSP);
    plain_box(c, 232, 20, 64, 64, LV_RADIUS_CIRCLE, COL_WARN, COL_TEXT, 2, LV_OPA_60);

    /* Ellipses: different width/height, still full radius. */
    plain_box(c, 32, 100, 96, 48, LV_RADIUS_CIRCLE, COL_ACCENT, 0, 0, LV_OPA_COVER);
    plain_box(c, 160, 100, 48, 72, LV_RADIUS_CIRCLE, COL_OK, 0, 0, LV_OPA_COVER);

    lv_obj_t *arc = lv_arc_create(c);
    lv_obj_set_size(arc, 72, 72);
    lv_obj_set_pos(arc, 240, 96);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 30, 280);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_HOT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_PANEL), LV_PART_MAIN);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *hint = lv_label_create(c);
    lv_label_set_text(hint, "circles / ellipses / arc wedge");
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_pos(hint, 16, 168);
}

static void page_composite(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    /* Rounded panel with inset triangle canvas — composite layering. */
    lv_obj_t *outer = plain_box(c, 40, 24, 240, 120, 16, COL_PANEL, COL_ACCENT, 2, LV_OPA_COVER);
    (void)outer;
    clear_fb(s_comp_tri_fb, TRI_PIX, rgb565(COL_PANEL));
    tri_fill_fb(s_comp_tri_fb, TRI_W, TRI_H, 40, 8, 8, 62, 72, 62, rgb565(COL_WARN), false, 0);
    lv_obj_t *tri = lv_canvas_create(c);
    lv_canvas_set_buffer(tri, s_comp_tri_fb, TRI_W, TRI_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(tri, 120, 52);
    lv_obj_remove_flag(tri, LV_OBJ_FLAG_CLICKABLE);

    plain_box(c, 260, 40, 40, 40, 8, COL_OK, 0, 0, LV_OPA_COVER);
    plain_box(c, 20, 40, 40, 40, LV_RADIUS_CIRCLE, COL_HOT, 0, 0, LV_OPA_COVER);

    lv_obj_t *hint = lv_label_create(c);
    lv_label_set_text(hint, "rounded box + triangle + corner badges");
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_pos(hint, 24, 160);
}

static void page_positioning(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    /* Same 48×48 marker at three alignment anchors. */
    plain_box(c, PAD, PAD, 48, 48, 6, COL_ACCENT, 0, 0, LV_OPA_COVER);
    lv_obj_t *ctr = plain_box(c, 0, 0, 48, 48, 6, COL_OK, 0, 0, LV_OPA_COVER);
    lv_obj_align(ctr, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *br = plain_box(c, 0, 0, 48, 48, 6, COL_WARN, 0, 0, LV_OPA_COVER);
    lv_obj_align(br, LV_ALIGN_BOTTOM_RIGHT, -PAD, -PAD);

    lv_obj_t *tl = lv_label_create(c);
    lv_label_set_text(tl, "TL (8,8)");
    lv_obj_set_style_text_color(tl, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_pos(tl, PAD, 60);

    lv_obj_t *cc = lv_label_create(c);
    lv_label_set_text(cc, "center");
    lv_obj_set_style_text_color(cc, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(cc, LV_ALIGN_CENTER, 0, 36);

    lv_obj_t *brl = lv_label_create(c);
    lv_label_set_text(brl, "bottom-right");
    lv_obj_set_style_text_color(brl, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(brl, LV_ALIGN_BOTTOM_RIGHT, -PAD, -36);
}

static void page_fonts(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);

    lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_AUTO);

    int y = 8;
    for (int px = 14; px <= 48; px += 2) {
        const lv_font_t *font = montserrat_for_px(px);
        char buf[32];
        snprintf(buf, sizeof(buf), "%d px — Montserrat", px);

        lv_obj_t *lab = lv_label_create(c);
        lv_label_set_text(lab, buf);
        lv_obj_set_pos(lab, 16, y);

        if (font) {
            lv_obj_set_style_text_font(lab, font, 0);
            lv_obj_set_style_text_color(lab, lv_color_hex(COL_TEXT), 0);
            y += lv_font_get_line_height(font) + 4;
        } else {
            lv_obj_set_style_text_color(lab, lv_color_hex(COL_MUTED), 0);
            y += 18;
        }
    }

    lv_obj_t *end = lv_obj_create(c);
    lv_obj_remove_style_all(end);
    lv_obj_set_size(end, 1, 8);
    lv_obj_set_pos(end, 0, y);
}

static void page_align_h(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    int y = 12;

    lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);

    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, "Left aligned line");
    lv_obj_set_style_text_color(l, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_width(l, 288);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(l, 16, y);
    y += 32;

    lv_obj_t *m = lv_label_create(c);
    lv_label_set_text(m, "Center title");
    lv_obj_set_style_text_color(m, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_text_font(m, font_medium(), 0);
    lv_obj_set_width(m, 288);
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(m, 16, y);
    y += 40;

    lv_obj_t *r = lv_label_create(c);
    lv_label_set_text(r, "42.7");
    lv_obj_set_style_text_color(r, lv_color_hex(COL_WARN), 0);
    lv_obj_set_width(r, 288);
    lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(r, 16, y);
    y += 36;

    lv_obj_t *lorem_l = lv_label_create(c);
    lv_label_set_text(
        lorem_l,
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Integer nec odio. Praesent libero.");
    lv_label_set_long_mode(lorem_l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lorem_l, 288);
    lv_obj_set_style_text_align(lorem_l, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(lorem_l, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_pos(lorem_l, 16, y);
    y += 72;

    lv_obj_t *lorem_c = lv_label_create(c);
    lv_label_set_text(lorem_c,
                      "Sed cursus ante dapibus diam. Sed nisi. Nulla quis sem at nibh elementum "
                      "imperdiet. Duis sagittis ipsum.");
    lv_label_set_long_mode(lorem_c, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lorem_c, 288);
    lv_obj_set_style_text_align(lorem_c, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lorem_c, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_pos(lorem_c, 16, y);
    y += 72;

    lv_obj_t *lorem_r = lv_label_create(c);
    lv_label_set_text(lorem_r,
                      "Facilisis in, dictum quis, adipiscing sed, diam. Cras ultricies mi eu turpis "
                      "hendrerit fringilla.");
    lv_label_set_long_mode(lorem_r, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lorem_r, 288);
    lv_obj_set_style_text_align(lorem_r, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(lorem_r, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_pos(lorem_r, 16, y);
    y += 72;

    lv_obj_t *end = lv_obj_create(c);
    lv_obj_remove_style_all(end);
    lv_obj_set_size(end, 1, 8);
    lv_obj_set_pos(end, 0, y);
}

static void page_align_v(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    /* Three 96×120 columns — text top / middle / bottom via align. */
    for (int i = 0; i < 3; i++) {
        int x = 16 + i * 104;
        lv_obj_t *col = plain_box(c, x, 8, 96, 120, 8, COL_PANEL, 0, 0, LV_OPA_COVER);
        lv_obj_t *lab = lv_label_create(col);
        lv_label_set_text(lab, i == 0 ? "top" : (i == 1 ? "mid" : "bot"));
        lv_obj_set_style_text_color(lab, lv_color_hex(COL_TEXT), 0);
        if (i == 0) {
            lv_obj_align(lab, LV_ALIGN_TOP_MID, 0, 5);
        } else if (i == 1) {
            lv_obj_align(lab, LV_ALIGN_CENTER, 0, 0);
        } else {
            lv_obj_align(lab, LV_ALIGN_BOTTOM_MID, 0, -5);
        }
    }
}

static void page_wrap(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    plain_box(c, 16, 12, 288, 120, 8, COL_PANEL, COL_ACCENT, 1, LV_OPA_COVER);
    lv_obj_t *lab = lv_label_create(c);
    lv_label_set_text(lab,
                      "This paragraph wraps inside a 288 px wide box. "
                      "Long mode WRAP keeps words on screen without clipping.");
    lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lab, 272);
    lv_obj_set_style_text_color(lab, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_pos(lab, 24, 24);
}

static void page_text_in_shape(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    lv_obj_t *box = plain_box(c, 24, 20, 120, 64, 10, COL_ACCENT, 0, 0, LV_OPA_COVER);
    (void)box;
    lv_obj_t *bl = lv_label_create(c);
    lv_label_set_text(bl, "BOX");
    lv_obj_set_style_text_color(bl, lv_color_hex(COL_BG), 0);
    lv_obj_align(bl, LV_ALIGN_TOP_LEFT, 68, 44);

    lv_obj_t *circ = plain_box(c, 180, 20, 72, 72, LV_RADIUS_CIRCLE, COL_OK, 0, 0, LV_OPA_COVER);
    (void)circ;
    lv_obj_t *cl = lv_label_create(c);
    lv_label_set_text(cl, "O");
    lv_obj_set_style_text_color(cl, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_text_font(cl, font_medium(), 0);
    lv_obj_align(cl, LV_ALIGN_TOP_LEFT, 204, 40);
}

static void page_icon_sizes(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    const char *sym = LV_SYMBOL_HOME;
    int sizes[] = {16, 32, 64};
    int xs[] = {40, 130, 230};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *lab = lv_label_create(c);
        lv_label_set_text(lab, sym);
        lv_obj_set_style_text_color(lab, lv_color_hex(COL_ACCENT), 0);
        lv_obj_set_pos(lab, xs[i], 40);
        lv_obj_set_style_transform_scale(lab, (256 * sizes[i]) / 16, 0);
        char cap[16];
        snprintf(cap, sizeof(cap), "%d px", sizes[i]);
        lv_obj_t *capl = lv_label_create(c);
        lv_label_set_text(capl, cap);
        lv_obj_set_style_text_color(capl, lv_color_hex(COL_MUTED), 0);
        lv_obj_set_pos(capl, xs[i] - 8, 110);
    }
}

static void page_img_pos(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    lv_obj_t *tl = lv_image_create(c);
    lv_image_set_src(tl, &s_sample_img);
    lv_obj_set_pos(tl, 8, 8);

    lv_obj_t *ctr = lv_image_create(c);
    lv_image_set_src(ctr, &s_sample_img);
    lv_obj_align(ctr, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *br = lv_image_create(c);
    lv_image_set_src(br, &s_sample_img);
    lv_obj_align(br, LV_ALIGN_BOTTOM_RIGHT, -8, -8);

    lv_obj_t *off = lv_image_create(c);
    lv_image_set_src(off, &s_sample_img);
    lv_obj_set_pos(off, 200, 24);
}

static void page_img_scale(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    lv_obj_t *a = lv_image_create(c);
    lv_image_set_src(a, &s_sample_img);
    lv_obj_set_pos(a, 20, 30);

    lv_obj_t *b = lv_image_create(c);
    lv_image_set_src(b, &s_sample_img);
    lv_image_set_scale(b, 384);
    lv_obj_set_pos(b, 100, 20);

    lv_obj_t *t = lv_image_create(c);
    lv_image_set_src(t, &s_sample_img);
    lv_image_set_scale(t, 512);
    lv_obj_set_pos(t, 210, 10);
    lv_obj_set_style_image_recolor(t, lv_color_hex(COL_HOT), 0);
    lv_obj_set_style_image_recolor_opa(t, LV_OPA_60, 0);

    lv_obj_t *hint = lv_label_create(c);
    lv_label_set_text(hint, "100% / 150% / 200% + tint");
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_pos(hint, 20, 130);
}

static void swatch_row(lv_obj_t *c, int y, const uint32_t *cols, int n, const char *label)
{
    for (int i = 0; i < n; i++) {
        plain_box(c, 16 + i * 48, y, 40, 40, 4, cols[i], 0, 0, LV_OPA_COVER);
    }
    lv_obj_t *lab = lv_label_create(c);
    lv_label_set_text(lab, label);
    lv_obj_set_style_text_color(lab, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_pos(lab, 16, y + 48);
}

static void page_pal_light(lv_obj_t *scr)
{
    style_screen(scr, 0xF0F2F5);
    lv_obj_set_style_bg_color(content_of(scr), lv_color_hex(0xF0F2F5), 0);
    uint32_t cols[] = {0xFFFFFF, 0xE8ECF0, 0xC8D0D8, 0x8898A8};
    swatch_row(content_of(scr), 20, cols, 4, "Light palette — high key bg");
}

static void page_pal_dark(lv_obj_t *scr)
{
    uint32_t cols[] = {0x101418, 0x1E242C, 0x2A3038, 0x3A4048};
    swatch_row(content_of(scr), 20, cols, 4, "Dark palette — product default");
}

static void page_pal_accent(lv_obj_t *scr)
{
    uint32_t cols[] = {0x5AA0E8, 0x7DCC7A, 0xE8C040, 0xE85A5A};
    swatch_row(content_of(scr), 20, cols, 4, "Accent colors — link / ok / warn / hot");
}

typedef struct {
    uint32_t a;
    uint32_t b;
    lv_grad_dir_t dir;
    int w;
    int h;
} demo_grad_t;

/* Shared palette for page 19 (Gradients) and 22 H snap grad. */
static const demo_grad_t s_demo_grads[] = {
    {0x5AA0E8, 0x101418, LV_GRAD_DIR_VER, 272, 72},
    {0xE85A5A, 0xE8C040, LV_GRAD_DIR_HOR, 272, 56},
    {0x7DCC7A, 0x2A4030, LV_GRAD_DIR_VER, 272, 48},
    {0x101418, 0x5AA0E8, LV_GRAD_DIR_HOR, 272, 40},
    {0xF0F2F5, 0x8898A8, LV_GRAD_DIR_VER, 272, 88},
    {0x2A2018, 0xE8C040, LV_GRAD_DIR_HOR, 272, 64},
    {0x142030, 0x7DCC7A, LV_GRAD_DIR_VER, 272, 52},
    {0xE85A5A, 0x5AA0E8, LV_GRAD_DIR_HOR, 272, 44},
    {0x1E242C, 0xE8F0E8, LV_GRAD_DIR_VER, 200, 36},
    {0x5AA0E8, 0xE85A5A, LV_GRAD_DIR_HOR, 200, 36},
    {0xE8C040, 0x7DCC7A, LV_GRAD_DIR_VER, 200, 36},
    {0x8898A8, 0x101418, LV_GRAD_DIR_HOR, 200, 36},
    {0x5AA0E8, 0xE8C040, LV_GRAD_DIR_VER, 128, 128},
    {0xE85A5A, 0x142030, LV_GRAD_DIR_HOR, 128, 128},
    {0x7DCC7A, 0xF0F2F5, LV_GRAD_DIR_VER, 128, 96},
    {0x2A3038, 0x5AA0E8, LV_GRAD_DIR_HOR, 160, 80},
    {0xE8C040, 0xE85A5A, LV_GRAD_DIR_VER, 272, 100},
    {0x101418, 0x7DCC7A, LV_GRAD_DIR_HOR, 272, 32},
    {0x5AA0E8, 0xE85A5A, LV_GRAD_DIR_VER, 132, 56},
    {0xE8C040, 0x7DCC7A, LV_GRAD_DIR_VER, 132, 56},
};

#define DEMO_GRAD_COUNT ((int)(sizeof(s_demo_grads) / sizeof(s_demo_grads[0])))

/* Optimized H snap grad: pre-baked RGB565 cards + tuned scroll (see page 33). */
#define SNAP_CARD_W      132
#define SNAP_CARD_H      100
#define SNAP_SCROLL_MS   250 /* ease-in-out: slow start + slow settle into lock */

static lv_obj_t *grad_box(lv_obj_t *parent, int x, int y, int w, int h, uint32_t c0, uint32_t c1,
                          lv_grad_dir_t dir)
{
    lv_obj_t *g = plain_box(parent, x, y, w, h, h > 40 ? 12 : 8, c0, 0, 0, LV_OPA_COVER);
    lv_obj_set_style_bg_grad_color(g, lv_color_hex(c1), 0);
    lv_obj_set_style_bg_grad_dir(g, dir, 0);
    return g;
}

static void page_gradient(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);

    lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);

    int y = 8;
    for (int i = 0; i < DEMO_GRAD_COUNT - 2; i++) {
        const demo_grad_t *g = &s_demo_grads[i];
        int x = (g->w >= 272) ? 24 : 24 + (int)((272 - g->w) / 2);
        grad_box(c, x, y, g->w, g->h, g->a, g->b, g->dir);
        y += g->h + 10;
    }
    const demo_grad_t *pair_l = &s_demo_grads[DEMO_GRAD_COUNT - 2];
    const demo_grad_t *pair_r = &s_demo_grads[DEMO_GRAD_COUNT - 1];
    grad_box(c, 24, y, pair_l->w, pair_l->h, pair_l->a, pair_l->b, pair_l->dir);
    grad_box(c, 164, y, pair_r->w, pair_r->h, pair_r->a, pair_r->b, pair_r->dir);
    y += pair_l->h + 10;

    lv_obj_t *end = lv_obj_create(c);
    lv_obj_remove_style_all(end);
    lv_obj_set_size(end, 1, 8);
    lv_obj_set_pos(end, 0, y);
}

static void page_themes(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    struct {
        const char *name;
        uint32_t bg;
        uint32_t fg;
    } themes[] = {
        {"warm", 0x2A2018, 0xF0D8A8},
        {"cool", 0x142030, 0xA8D0F0},
        {"hi-contrast", 0x000000, 0xFFFFFF},
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *card = plain_box(c, 16, 12 + i * 56, 288, 48, 8, themes[i].bg, 0, 0, LV_OPA_COVER);
        (void)card;
        lv_obj_t *lab = lv_label_create(c);
        lv_label_set_text(lab, themes[i].name);
        lv_obj_set_style_text_color(lab, lv_color_hex(themes[i].fg), 0);
        lv_obj_set_pos(lab, 28, 24 + i * 56);
    }
}

static void page_scroll_v(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    for (int i = 0; i < 12; i++) {
        char buf[24];
        snprintf(buf, sizeof(buf), "row %d", i + 1);
        lv_obj_t *row =
            plain_box(c, 16, 8 + i * (ROW_H + ROW_GAP), 288, ROW_H, 6, COL_PANEL, 0, 0, LV_OPA_COVER);
        lv_obj_t *lab = lv_label_create(row);
        lv_label_set_text(lab, buf);
        lv_obj_set_style_text_color(lab, lv_color_hex(COL_TEXT), 0);
        lv_obj_center(lab);
    }
}

typedef struct {
    int card_w;
    int card_h;
    int gap;
    int count;
    bool snap_center;
    bool grad_cards;
} h_scroll_cfg_t;

static uint16_t *s_baked_grad[DEMO_GRAD_COUNT];
static int64_t s_snap_t0;
static int32_t s_snap_target;

static unsigned lerp_u8(unsigned a, unsigned b, int t, int tmax)
{
    if (tmax <= 0) {
        return a;
    }
    if (t < 0) {
        t = 0;
    }
    if (t > tmax) {
        t = tmax;
    }
    return (unsigned)((int)a + ((int)b - (int)a) * t / tmax);
}

static uint16_t lerp565_hex(uint32_t h0, uint32_t h1, int t, int tmax)
{
    unsigned r = lerp_u8((h0 >> 16) & 0xFFu, (h1 >> 16) & 0xFFu, t, tmax);
    unsigned g = lerp_u8((h0 >> 8) & 0xFFu, (h1 >> 8) & 0xFFu, t, tmax);
    unsigned b = lerp_u8(h0 & 0xFFu, h1 & 0xFFu, t, tmax);
    return rgb565((r << 16) | (g << 8) | b);
}

static uint16_t *baked_grad_buf(int idx)
{
    const demo_grad_t *g;
    int tmax;

    if (idx < 0 || idx >= DEMO_GRAD_COUNT) {
        return NULL;
    }
    if (s_baked_grad[idx] != NULL) {
        return s_baked_grad[idx];
    }
    s_baked_grad[idx] =
        heap_caps_malloc(SNAP_CARD_W * SNAP_CARD_H * sizeof(uint16_t),
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_baked_grad[idx] == NULL) {
        s_baked_grad[idx] =
            heap_caps_malloc(SNAP_CARD_W * SNAP_CARD_H * sizeof(uint16_t),
                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_baked_grad[idx] == NULL) {
        return NULL;
    }
    g = &s_demo_grads[idx];
    tmax = (g->dir == LV_GRAD_DIR_HOR) ? (SNAP_CARD_W - 1) : (SNAP_CARD_H - 1);
    if (tmax < 1) {
        tmax = 1;
    }
    for (int y = 0; y < SNAP_CARD_H; y++) {
        for (int x = 0; x < SNAP_CARD_W; x++) {
            int t = (g->dir == LV_GRAD_DIR_HOR) ? x : y;
            s_baked_grad[idx][y * SNAP_CARD_W + x] = lerp565_hex(g->a, g->b, t, tmax);
        }
    }
    return s_baked_grad[idx];
}

static int32_t snap_target_x(lv_obj_t *card, lv_obj_t *scroller)
{
    int32_t card_x = lv_obj_get_x(card);
    int32_t card_w = lv_obj_get_width(card);
    int32_t view_w = lv_obj_get_width(scroller);
    int32_t target = card_x + card_w / 2 - view_w / 2;
    int32_t content_right = 0;
    uint32_t n = lv_obj_get_child_cnt(scroller);

    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *ch = lv_obj_get_child(scroller, i);
        int32_t right = lv_obj_get_x(ch) + lv_obj_get_width(ch);
        if (right > content_right) {
            content_right = right;
        }
    }
    int32_t max_x = content_right - view_w;
    if (max_x < 0) {
        max_x = 0;
    }
    if (target < 0) {
        target = 0;
    }
    if (target > max_x) {
        target = max_x;
    }
    return target;
}

static void scroll_x_exec(void *obj, int32_t v)
{
    lv_obj_scroll_to_x((lv_obj_t *)obj, v, LV_ANIM_OFF);
}

static void snap_anim_done(lv_anim_t *a)
{
    lv_obj_t *scroller = (lv_obj_t *)lv_anim_get_user_data(a);
    int ms = (int)((esp_timer_get_time() - s_snap_t0) / 1000);

    if (scroller != NULL) {
        /* Hard lock — no sub-pixel drift after the eased glide. */
        lv_obj_scroll_to_x(scroller, s_snap_target, LV_ANIM_OFF);
    }
    ESP_LOGI(TAG, "snap scroll %d ms (target %d ms, lock x=%ld)", ms, SNAP_SCROLL_MS,
             (long)s_snap_target);
}

static void snap_scroll_tuned(lv_obj_t *scroller, int32_t target)
{
    int32_t start = lv_obj_get_scroll_x(scroller);

    lv_anim_delete(scroller, scroll_x_exec);
    s_snap_target = target;
    s_snap_t0 = esp_timer_get_time();
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, scroller);
    lv_anim_set_user_data(&a, scroller);
    lv_anim_set_values(&a, start, target);
    lv_anim_set_duration(&a, SNAP_SCROLL_MS);
    lv_anim_set_exec_cb(&a, scroll_x_exec);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, snap_anim_done);
    lv_anim_start(&a);
}

static void on_h_card_snap(lv_event_t *e)
{
    lv_obj_t *card = lv_event_get_current_target(e);
    lv_obj_t *scroller = (lv_obj_t *)lv_event_get_user_data(e);
    if (card == NULL || scroller == NULL) {
        return;
    }
    lv_obj_scroll_to_x(scroller, snap_target_x(card, scroller), LV_ANIM_ON);
}

static void on_h_card_snap_opt(lv_event_t *e)
{
    lv_obj_t *card = lv_event_get_current_target(e);
    lv_obj_t *scroller = (lv_obj_t *)lv_event_get_user_data(e);
    if (card == NULL || scroller == NULL) {
        return;
    }
    snap_scroll_tuned(scroller, snap_target_x(card, scroller));
}

static void build_h_scroll(lv_obj_t *scr, const h_scroll_cfg_t *cfg)
{
    lv_obj_t *c = content_of(scr);
    int step = cfg->card_w + cfg->gap;
    int y = (CONTENT_H - cfg->card_h) / 2;
    if (y < 8) {
        y = 8;
    }

    lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(c, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_AUTO);

    for (int i = 0; i < cfg->count; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "grad %d", i + 1);
        lv_obj_t *col;
        if (cfg->grad_cards) {
            const demo_grad_t *g = &s_demo_grads[i];
            col = plain_box(c, cfg->gap + i * step, y, cfg->card_w, cfg->card_h, 8, g->a, 0, 0,
                            LV_OPA_COVER);
            lv_obj_set_style_bg_grad_color(col, lv_color_hex(g->b), 0);
            lv_obj_set_style_bg_grad_dir(col, g->dir, 0);
        } else {
            col = plain_box(c, cfg->gap + i * step, y, cfg->card_w, cfg->card_h, 8, COL_PANEL,
                            COL_ACCENT, 1, LV_OPA_COVER);
        }
        if (cfg->snap_center) {
            lv_obj_add_flag(col, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(col, on_h_card_snap, LV_EVENT_CLICKED, c);
        }
        lv_obj_t *lab = lv_label_create(col);
        lv_label_set_text(lab, buf);
        lv_obj_set_style_text_color(lab, lv_color_hex(COL_TEXT), 0);
        lv_obj_center(lab);
    }

    lv_obj_t *end = lv_obj_create(c);
    lv_obj_remove_style_all(end);
    lv_obj_set_size(end, 1, 1);
    lv_obj_set_pos(end, cfg->gap + cfg->count * step, 0);
}

static void page_scroll_h(lv_obj_t *scr)
{
    const h_scroll_cfg_t cfg = {88, 100, 12, 8, false, false};
    build_h_scroll(scr, &cfg);
}

static void page_scroll_h_tall(lv_obj_t *scr)
{
    const h_scroll_cfg_t cfg = {88, 160, 12, 8, false, false};
    build_h_scroll(scr, &cfg);
}

static void page_scroll_h_wide(lv_obj_t *scr)
{
    const h_scroll_cfg_t cfg = {132, 100, 12, 8, false, false};
    build_h_scroll(scr, &cfg);
}

static void page_scroll_h_snap(lv_obj_t *scr)
{
    /* Same 132 px wide cards as H wide, with tap-to-center scroll. */
    const h_scroll_cfg_t cfg = {132, 100, 12, 8, true, false};
    build_h_scroll(scr, &cfg);
}

static void page_scroll_h_snap_grad(lv_obj_t *scr)
{
    const h_scroll_cfg_t cfg = {132, 100, 12, DEMO_GRAD_COUNT, true, true};
    build_h_scroll(scr, &cfg);
}

static void build_h_scroll_baked(lv_obj_t *scr, const h_scroll_cfg_t *cfg)
{
    lv_obj_t *c = content_of(scr);
    int step = cfg->card_w + cfg->gap;
    int y = (CONTENT_H - cfg->card_h) / 2;
    if (y < 8) {
        y = 8;
    }

    lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(c, LV_DIR_HOR);
    /* Hide scrollbar — fewer partial redraws while cards move. */
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < cfg->count; i++) {
        char buf[16];
        uint16_t *fb = baked_grad_buf(i);
        lv_obj_t *col;

        snprintf(buf, sizeof(buf), "grad %d", i + 1);
        if (fb != NULL) {
            col = lv_canvas_create(c);
            lv_canvas_set_buffer(col, fb, SNAP_CARD_W, SNAP_CARD_H, LV_COLOR_FORMAT_RGB565);
            lv_obj_set_pos(col, cfg->gap + i * step, y);
            lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        } else {
            const demo_grad_t *g = &s_demo_grads[i];
            col = plain_box(c, cfg->gap + i * step, y, cfg->card_w, cfg->card_h, 8, g->a, 0, 0,
                            LV_OPA_COVER);
            lv_obj_set_style_bg_grad_color(col, lv_color_hex(g->b), 0);
            lv_obj_set_style_bg_grad_dir(col, g->dir, 0);
        }
        lv_obj_add_flag(col, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(col, on_h_card_snap_opt, LV_EVENT_CLICKED, c);
        lv_obj_t *lab = lv_label_create(col);
        lv_label_set_text(lab, buf);
        lv_obj_set_style_text_color(lab, lv_color_hex(COL_TEXT), 0);
        lv_obj_center(lab);
    }

    lv_obj_t *end = lv_obj_create(c);
    lv_obj_remove_style_all(end);
    lv_obj_set_size(end, 1, 1);
    lv_obj_set_pos(end, cfg->gap + cfg->count * step, 0);

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "baked RGB565 · 250 ms ease-in-out lock");
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
}

static void page_scroll_h_snap_grad_opt(lv_obj_t *scr)
{
    const h_scroll_cfg_t cfg = {132, 100, 12, DEMO_GRAD_COUNT, true, false};
    build_h_scroll_baked(scr, &cfg);
}

static void page_scroll_list(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    lv_obj_add_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(c, LV_DIR_VER);
    const char *icons[] = {LV_SYMBOL_HOME, LV_SYMBOL_BELL, LV_SYMBOL_AUDIO, LV_SYMBOL_SETTINGS};
    for (int i = 0; i < 10; i++) {
        int y = 8 + i * (LIST_ROW_H + ROW_GAP);
        lv_obj_t *row = plain_box(c, 12, y, 296, LIST_ROW_H, 6, COL_PANEL, 0, 0, LV_OPA_COVER);
        lv_obj_t *ic = lv_label_create(row);
        lv_label_set_text(ic, icons[i % 4]);
        lv_obj_set_style_text_color(ic, lv_color_hex(COL_ACCENT), 0);
        lv_obj_set_pos(ic, 8, (LIST_ROW_H - 16) / 2);
        lv_obj_t *tx = lv_label_create(row);
        char buf[24];
        snprintf(buf, sizeof(buf), "item %d", i + 1);
        lv_label_set_text(tx, buf);
        lv_obj_set_style_text_color(tx, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_pos(tx, 36, (LIST_ROW_H - 16) / 2);
    }
}

static void page_scroll_chrome(lv_obj_t *scr)
{
    /* Fixed header + footer; only the middle list scrolls (20 items). */
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, SCR_W, 32);
    lv_obj_set_pos(hdr, 0, CHROME_H);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_t *ht = lv_label_create(hdr);
    lv_label_set_text(ht, "Fixed header");
    lv_obj_set_style_text_color(ht, lv_color_hex(COL_BG), 0);
    lv_obj_center(ht);

    lv_obj_t *ftr = lv_obj_create(scr);
    lv_obj_remove_style_all(ftr);
    lv_obj_set_size(ftr, SCR_W, 24);
    lv_obj_align(ftr, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(ftr, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(ftr, LV_OPA_COVER, 0);
    lv_obj_t *ft = lv_label_create(ftr);
    lv_label_set_text(ft, "Fixed footer");
    lv_obj_set_style_text_color(ft, lv_color_hex(COL_MUTED), 0);
    lv_obj_center(ft);

    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 0, CHROME_H + 32);
    lv_obj_set_size(list, SCR_W, CONTENT_H - 32 - 24);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);

    for (int i = 0; i < 20; i++) {
        char buf[24];
        snprintf(buf, sizeof(buf), "entry %02d", i + 1);
        lv_obj_t *row =
            plain_box(list, 12, 4 + i * (CHROME_ROW_H + 4), 296, CHROME_ROW_H, 4, COL_PANEL, 0, 0,
                      LV_OPA_COVER);
        lv_obj_t *lab = lv_label_create(row);
        lv_label_set_text(lab, buf);
        lv_obj_set_style_text_color(lab, lv_color_hex(COL_TEXT), 0);
        lv_obj_center(lab);
    }
    lv_obj_t *end = lv_obj_create(list);
    lv_obj_remove_style_all(end);
    lv_obj_set_size(end, 1, 4);
    lv_obj_set_pos(end, 0, 4 + 20 * (CHROME_ROW_H + 4));
}

static void page_overlap(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    plain_box(c, 24, 24, 200, 120, 8, COL_PANEL, 0, 0, LV_OPA_COVER);
    plain_box(c, 80, 60, 200, 100, 8, COL_ACCENT, 0, 0, LV_OPA_70);
    lv_obj_t *lab = lv_label_create(c);
    lv_label_set_text(lab, "floating +20/+20");
    lv_obj_set_style_text_color(lab, lv_color_hex(COL_BG), 0);
    lv_obj_set_pos(lab, 96, 100);
}

static void page_zindex(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    lv_obj_t *back = plain_box(c, 40, 30, 160, 100, 8, COL_PANEL, 0, 0, LV_OPA_COVER);
    lv_obj_t *mid = plain_box(c, 100, 60, 160, 100, 8, COL_ACCENT, 0, 0, LV_OPA_50);
    lv_obj_t *front = plain_box(c, 160, 90, 120, 72, 8, COL_WARN, 0, 0, LV_OPA_COVER);
    (void)back;
    (void)mid;
    lv_obj_move_foreground(front);
    lv_obj_t *lab = lv_label_create(c);
    lv_label_set_text(lab, "50% overlay + foreground");
    lv_obj_set_style_text_color(lab, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_pos(lab, 40, 150);
}

static void page_modal(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    plain_box(c, 16, 16, 288, 140, 8, COL_PANEL, 0, 0, LV_OPA_COVER);
    lv_obj_t *base = lv_label_create(c);
    lv_label_set_text(base, "Base content behind modal");
    lv_obj_set_style_text_color(base, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_pos(base, 24, 24);

    lv_obj_t *dim = lv_obj_create(scr);
    lv_obj_remove_style_all(dim);
    lv_obj_set_size(dim, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(dim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(dim, LV_OPA_50, 0);
    lv_obj_remove_flag(dim, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *modal = plain_box(scr, 60, 60, 200, 100, 12, COL_PANEL, COL_ACCENT, 2, LV_OPA_COVER);
    (void)modal;
    lv_obj_t *mt = lv_label_create(scr);
    lv_label_set_text(mt, "Modal panel");
    lv_obj_set_style_text_color(mt, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(mt, LV_ALIGN_CENTER, 0, -8);
    lv_obj_t *ms = lv_label_create(scr);
    lv_label_set_text(ms, "dimmed bg 50%");
    lv_obj_set_style_text_color(ms, lv_color_hex(COL_MUTED), 0);
    lv_obj_align(ms, LV_ALIGN_CENTER, 0, 16);
}

static void page_modal_scroll(lv_obj_t *scr)
{
    lv_obj_t *c = content_of(scr);
    plain_box(c, 16, 16, 288, 140, 8, COL_PANEL, 0, 0, LV_OPA_COVER);
    lv_obj_t *base = lv_label_create(c);
    lv_label_set_text(base, "Base content behind modal");
    lv_obj_set_style_text_color(base, lv_color_hex(COL_MUTED), 0);
    lv_obj_set_pos(base, 24, 24);

    lv_obj_t *dim = lv_obj_create(scr);
    lv_obj_remove_style_all(dim);
    lv_obj_set_size(dim, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(dim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(dim, LV_OPA_50, 0);
    lv_obj_remove_flag(dim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(dim, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *modal = plain_box(scr, 36, 44, 248, 168, 12, COL_PANEL, COL_ACCENT, 2, LV_OPA_COVER);
    lv_obj_remove_flag(modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(modal);
    lv_label_set_text(title, "Scrollable modal");
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *body = lv_obj_create(modal);
    lv_obj_remove_style_all(body);
    lv_obj_set_pos(body, 12, 32);
    lv_obj_set_size(body, 224, 124);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(body, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_40, 0);
    lv_obj_set_style_radius(body, 6, 0);
    lv_obj_set_style_pad_all(body, 4, 0);

    for (int i = 0; i < 14; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Modal row %02d — lorem ipsum dolor sit amet", i + 1);
        lv_obj_t *row = lv_label_create(body);
        lv_label_set_text(row, buf);
        lv_label_set_long_mode(row, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(row, 208);
        lv_obj_set_style_text_color(row, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_pos(row, 0, i * 40);
    }

    lv_obj_t *end = lv_obj_create(body);
    lv_obj_remove_style_all(end);
    lv_obj_set_size(end, 1, 4);
    lv_obj_set_pos(end, 0, 14 * 40);
}

static void build_menu(void)
{
    static const struct {
        const char *section;
        int page;
        const char *label;
    } items[] = {
        {"SHAPES", PAGE_RECT, "01 Rectangles"},
        {"SHAPES", PAGE_ROUND, "02 Rounded boxes"},
        {"SHAPES", PAGE_TRI, "03 Triangles"},
        {"SHAPES", PAGE_CIRC, "04 Circles & ellipses"},
        {"SHAPES", PAGE_COMP, "05 Composite"},
        {"SHAPES", PAGE_POS, "06 Positioning"},
        {"TEXT", PAGE_FONTS, "07 Font sizes"},
        {"TEXT", PAGE_ALIGN_H, "08 Align H"},
        {"TEXT", PAGE_ALIGN_V, "09 Align V"},
        {"TEXT", PAGE_WRAP, "10 Wrap"},
        {"TEXT", PAGE_TEXT_IN_SHAPE, "11 Text in shapes"},
        {"ICONS", PAGE_ICON_SIZES, "12 Icon sizes"},
        {"ICONS", PAGE_IMG_POS, "13 Image position"},
        {"ICONS", PAGE_IMG_SCALE, "14 Scale & tint"},
        {"COLORS", PAGE_PAL_LIGHT, "16 Light palette"},
        {"COLORS", PAGE_PAL_DARK, "17 Dark palette"},
        {"COLORS", PAGE_PAL_ACCENT, "18 Accents"},
        {"COLORS", PAGE_GRADIENT, "19 Gradients"},
        {"COLORS", PAGE_THEMES, "20 Themes"},
        {"SCROLL", PAGE_SCROLL_V, "21 Vertical scroll"},
        {"SCROLL", PAGE_SCROLL_H, "22 H cards"},
        {"SCROLL", PAGE_SCROLL_H_TALL, "22 H tall cols"},
        {"SCROLL", PAGE_SCROLL_H_WIDE, "22 H wide cols"},
        {"SCROLL", PAGE_SCROLL_H_SNAP, "22 H snap center"},
        {"SCROLL", PAGE_SCROLL_H_SNAP_GRAD, "22 H snap gradients"},
        {"SCROLL", PAGE_SCROLL_H_SNAP_GRAD_OPT, "22 H snap grad opt"},
        {"SCROLL", PAGE_SCROLL_LIST, "23 Icon list"},
        {"SCROLL", PAGE_SCROLL_CHROME, "24 Header/footer list"},
        {"LAYERS", PAGE_OVERLAP, "25 Overlap"},
        {"LAYERS", PAGE_ZINDEX, "26 Z-index / opacity"},
        {"LAYERS", PAGE_MODAL, "27 Modal"},
        {"LAYERS", PAGE_MODAL_SCROLL, "28 Modal scroll"},
    };

    s_screens[PAGE_MENU] = lv_obj_create(NULL);
    style_screen(s_screens[PAGE_MENU], COL_BG);

    lv_obj_t *title = lv_label_create(s_screens[PAGE_MENU]);
    lv_label_set_text(title, "p13 UI showcase");
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(title, font_medium(), 0);
    lv_obj_set_pos(title, PAD, 8);

    lv_obj_t *list = lv_obj_create(s_screens[PAGE_MENU]);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 0, 40);
    lv_obj_set_size(list, SCR_W, SCR_H - 40);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);

    const char *last_sec = "";
    int y = 4;
    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
        if (strcmp(last_sec, items[i].section) != 0) {
            lv_obj_t *sec = lv_label_create(list);
            lv_label_set_text(sec, items[i].section);
            lv_obj_set_style_text_color(sec, lv_color_hex(COL_ACCENT), 0);
            lv_obj_set_pos(sec, PAD, y);
            y += 22;
            last_sec = items[i].section;
        }
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(row, 296, MENU_ROW_H - 4);
        lv_obj_set_pos(row, 12, y);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(COL_PANEL), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(row, on_menu_pick, LV_EVENT_CLICKED, (void *)(intptr_t)items[i].page);

        lv_obj_t *lab = lv_label_create(row);
        lv_label_set_text(lab, items[i].label);
        lv_obj_set_style_text_color(lab, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_pos(lab, 8, 6);
        y += MENU_ROW_H;
    }
    lv_obj_t *sp = lv_obj_create(list);
    lv_obj_remove_style_all(sp);
    lv_obj_set_size(sp, 1, 8);
    lv_obj_set_pos(sp, 0, y);
}

static const page_info_t s_page_table[] = {
    {PAGE_RECT, 1, "Rectangles", page_rectangles},
    {PAGE_ROUND, 2, "Rounded", page_rounded},
    {PAGE_TRI, 3, "Triangles", page_triangles},
    {PAGE_CIRC, 4, "Circles", page_circles},
    {PAGE_COMP, 5, "Composite", page_composite},
    {PAGE_POS, 6, "Position", page_positioning},
    {PAGE_FONTS, 7, "Fonts", page_fonts},
    {PAGE_ALIGN_H, 8, "Align H", page_align_h},
    {PAGE_ALIGN_V, 9, "Align V", page_align_v},
    {PAGE_WRAP, 10, "Wrap", page_wrap},
    {PAGE_TEXT_IN_SHAPE, 11, "In shape", page_text_in_shape},
    {PAGE_ICON_SIZES, 12, "Icon sizes", page_icon_sizes},
    {PAGE_IMG_POS, 13, "Img pos", page_img_pos},
    {PAGE_IMG_SCALE, 14, "Scale", page_img_scale},
    {PAGE_PAL_LIGHT, 16, "Light", page_pal_light},
    {PAGE_PAL_DARK, 17, "Dark", page_pal_dark},
    {PAGE_PAL_ACCENT, 18, "Accent", page_pal_accent},
    {PAGE_GRADIENT, 19, "Gradients", page_gradient},
    {PAGE_THEMES, 20, "Themes", page_themes},
    {PAGE_SCROLL_V, 21, "Vertical", page_scroll_v},
    {PAGE_SCROLL_H, 22, "H cards", page_scroll_h},
    {PAGE_SCROLL_H_TALL, 22, "H tall", page_scroll_h_tall},
    {PAGE_SCROLL_H_WIDE, 22, "H wide", page_scroll_h_wide},
    {PAGE_SCROLL_H_SNAP, 22, "H snap", page_scroll_h_snap},
    {PAGE_SCROLL_H_SNAP_GRAD, 22, "H snap grad", page_scroll_h_snap_grad},
    {PAGE_SCROLL_H_SNAP_GRAD_OPT, 22, "H snap fast", page_scroll_h_snap_grad_opt},
    {PAGE_SCROLL_LIST, 23, "List", page_scroll_list},
    {PAGE_SCROLL_CHROME, 24, "Chrome", page_scroll_chrome},
    {PAGE_OVERLAP, 25, "Overlap", page_overlap},
    {PAGE_ZINDEX, 26, "Z-index", page_zindex},
    {PAGE_MODAL, 27, "Modal", page_modal},
    {PAGE_MODAL_SCROLL, 28, "Modal scroll", page_modal_scroll},
};

static const page_info_t *page_info_for(int idx)
{
    for (size_t i = 0; i < PAGE_TABLE_COUNT; i++) {
        if (s_page_table[i].page == idx) {
            return &s_page_table[i];
        }
    }
    return NULL;
}

static void drop_demo_screen(int idx)
{
    if (idx <= PAGE_MENU || idx >= PAGE_COUNT || s_screens[idx] == NULL) {
        return;
    }
    lv_obj_delete(s_screens[idx]);
    s_screens[idx] = NULL;
    s_content[idx] = NULL;
}

static void ensure_page(int idx)
{
    const page_info_t *p;

    if (idx == PAGE_MENU || s_screens[idx] != NULL) {
        return;
    }
    if (!s_sample_ready) {
        init_sample_image();
        s_sample_ready = true;
    }
    p = page_info_for(idx);
    if (p == NULL) {
        return;
    }
    s_screens[idx] = new_demo_screen(idx, p->id, p->title, COL_BG); /* idx == p->page */
    if (s_screens[idx] == NULL) {
        ESP_LOGE(TAG, "screen alloc failed page %d", idx);
        return;
    }
    p->build(s_screens[idx]);
    ESP_LOGI(TAG, "built page %02d heap=%u", idx, (unsigned)esp_get_free_heap_size());
}

static void build_startup(void)
{
    build_menu();
    ESP_LOGI(TAG, "menu ready heap=%u", (unsigned)esp_get_free_heap_size());
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("p13", "display");
        return;
    }
    if (!board_lvgl_lock(2000)) {
        demo_fail("p13", "lvgl lock");
        return;
    }
    build_startup();
    go_page(PAGE_MENU);
    board_lvgl_unlock();

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    if (bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM) == ESP_OK) {
        if (btns[BSP_BUTTON_CONFIG]) {
            iot_button_register_cb(btns[BSP_BUTTON_CONFIG], BUTTON_PRESS_DOWN, NULL, on_boot_btn,
                                  NULL);
        }
        if (btns[BSP_BUTTON_MAIN]) {
            iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_UP, NULL, on_circle_btn,
                                  NULL);
        }
    }

    ESP_LOGI(TAG, "%u pages. Menu tap or boot/circle = next.", (unsigned)PAGE_TABLE_COUNT);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));
        (void)s_saw_next;
    }
}
