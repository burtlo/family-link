/*
 * h28 — Short clip on the 320×240 LCD.
 *
 * Island demo: no Wi-Fi, no JPEG decoder, no speaker. Paints a ~5 s film
 * (title → bouncing-ball scene → end card) into a 320×240 RGB565 buffer
 * and blits it the same way h13 / p12 do. Loops. Proves the SPI LCD can
 * hold a watchable frame rate for a short moving picture.
 *
 * Product v1 is still not video. This is the glass budget check.
 *
 * -- PASS h28 after the first complete playthrough.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "board.h"
#include "pass.h"

static const char *TAG = "h28";

#define W            320
#define H            240
#define FB_BYTES     (W * H * 2)
#define FPS          12
#define FRAME_MS     (1000 / FPS)
#define CLIP_FRAMES  60 /* 5.0 s @ 12 fps */
#define TITLE_END    12
#define SCENE_END    48

#define COL_NAVY     0x101828
#define COL_CARD     0xE8F0E8
#define COL_GOLD     0xD4B45A
#define COL_SPROCKET 0x1A1A1A
#define COL_BALL     0xE24A3C
#define COL_BALL_H   0xF4A090
#define COL_SHADOW   0x2A3040
#define COL_SUN      0xF4D45A
#define COL_HILL     0x3A8A52
#define COL_HILL2    0x2A6A40
#define COL_CLOUD    0xF0F4F8
#define COL_BAR      0xF4D45A

static uint16_t *s_fb;
static lv_image_dsc_t s_dsc;
static lv_obj_t *s_img;
static int s_frame;
static bool s_passed;
static int64_t s_loop_start_us;
static int64_t s_max_us;

/* 5×7, bits 4..0 = left..right. */
static const uint8_t k_c[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
static const uint8_t k_l[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
static const uint8_t k_i[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
static const uint8_t k_p[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
static const uint8_t k_e[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
static const uint8_t k_n[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
static const uint8_t k_d[7] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};

static uint16_t rgb565(uint32_t hex)
{
    unsigned r = (hex >> 16) & 0xFFu;
    unsigned g = (hex >> 8) & 0xFFu;
    unsigned b = hex & 0xFFu;
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

static unsigned lerp_u(unsigned a, unsigned b, int t, int tmax)
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

static uint16_t lerp_rgb(uint32_t a, uint32_t b, int t, int tmax)
{
    unsigned r = lerp_u((a >> 16) & 0xFFu, (b >> 16) & 0xFFu, t, tmax);
    unsigned g = lerp_u((a >> 8) & 0xFFu, (b >> 8) & 0xFFu, t, tmax);
    unsigned bl = lerp_u(a & 0xFFu, b & 0xFFu, t, tmax);
    return rgb565((r << 16) | (g << 8) | bl);
}

static void fill_rect(int x0, int y0, int x1, int y1, uint16_t c)
{
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > W - 1) {
        x1 = W - 1;
    }
    if (y1 > H - 1) {
        y1 = H - 1;
    }
    if (x0 > x1 || y0 > y1) {
        return;
    }
    for (int y = y0; y <= y1; y++) {
        uint16_t *row = s_fb + y * W;
        for (int x = x0; x <= x1; x++) {
            row[x] = c;
        }
    }
}

static void fill_row(int y, int x0, int x1, uint16_t c)
{
    if (y < 0 || y >= H) {
        return;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 > W - 1) {
        x1 = W - 1;
    }
    uint16_t *row = s_fb + y * W;
    for (int x = x0; x <= x1; x++) {
        row[x] = c;
    }
}

static void fill_ellipse(int cx, int cy, int rx, int ry, uint16_t c)
{
    if (rx < 1 || ry < 1) {
        return;
    }
    int y0 = cy - ry;
    int y1 = cy + ry;
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > H - 1) {
        y1 = H - 1;
    }
    float rx2 = (float)(rx * rx);
    float ry2 = (float)(ry * ry);
    for (int y = y0; y <= y1; y++) {
        float dy = (float)(y - cy);
        float inner = 1.0f - (dy * dy) / ry2;
        if (inner < 0.0f) {
            continue;
        }
        int span = (int)(sqrtf(inner * rx2) + 0.5f);
        fill_row(y, cx - span, cx + span, c);
    }
}

static void fill_circle(int cx, int cy, int r, uint16_t c)
{
    fill_ellipse(cx, cy, r, r, c);
}

static void blit_glyph(int x, int y, const uint8_t *rows, int scale, uint16_t c)
{
    for (int r = 0; r < 7; r++) {
        for (int b = 0; b < 5; b++) {
            if (rows[r] & (1u << (4 - b))) {
                fill_rect(x + b * scale, y + r * scale,
                          x + (b + 1) * scale - 1, y + (r + 1) * scale - 1, c);
            }
        }
    }
}

static void blit_word(int x, int y, const uint8_t *const *glyphs, int n, int scale, uint16_t c)
{
    int gap = scale;
    for (int i = 0; i < n; i++) {
        blit_glyph(x + i * (5 * scale + gap), y, glyphs[i], scale, c);
    }
}

static void film_border(void)
{
    uint16_t navy = rgb565(COL_NAVY);
    uint16_t gold = rgb565(COL_GOLD);
    uint16_t hole = rgb565(COL_SPROCKET);
    fill_rect(0, 0, 15, H - 1, navy);
    fill_rect(W - 16, 0, W - 1, H - 1, navy);
    for (int y = 6; y < H - 8; y += 20) {
        fill_rect(4, y, 11, y + 8, gold);
        fill_rect(6, y + 2, 9, y + 6, hole);
        fill_rect(W - 12, y, W - 5, y + 8, gold);
        fill_rect(W - 10, y + 2, W - 7, y + 6, hole);
    }
}

static void playhead_bar(int frame)
{
    int inner = W - 40;
    int filled = inner * frame / CLIP_FRAMES;
    fill_rect(20, H - 10, W - 21, H - 6, rgb565(0x202830));
    if (filled > 0) {
        fill_rect(20, H - 10, 20 + filled, H - 6, rgb565(COL_BAR));
    }
}

static void paint_title(int frame)
{
    uint16_t navy = rgb565(COL_NAVY);
    for (int y = 0; y < H; y++) {
        fill_row(y, 0, W - 1, navy);
    }
    film_border();

    const uint8_t *clip[] = {k_c, k_l, k_i, k_p};
    int scale = 8;
    int word_w = 4 * (5 * scale) + 3 * scale;
    int word_h = 7 * scale;
    int x = (W - word_w) / 2;
    int y = (H - word_h) / 2 - 8;
    int fade = frame < 6 ? frame : 6;
    uint16_t ink = lerp_rgb(COL_NAVY, COL_CARD, fade, 6);
    blit_word(x, y, clip, 4, scale, ink);

    /* Play triangle under the title. */
    int cx = W / 2 - 4;
    int cy = y + word_h + 22;
    uint16_t tri = lerp_rgb(COL_NAVY, COL_GOLD, fade, 6);
    for (int i = 0; i < 14; i++) {
        fill_row(cy - 7 + i, cx, cx + i, tri);
    }
    playhead_bar(frame);
}

static void paint_scene(int frame)
{
    int t = frame - TITLE_END;
    int tmax = SCENE_END - TITLE_END - 1;
    int horizon = 150;

    for (int y = 0; y < H; y++) {
        uint32_t day = (y < horizon)
                           ? ((lerp_u(0x4A, 0xF4, y, horizon) << 16) |
                              (lerp_u(0x90, 0xC8, y, horizon) << 8) |
                              lerp_u(0xD4, 0x78, y, horizon))
                           : 0xF4C878;
        uint32_t dusk = (y < horizon)
                            ? ((lerp_u(0x2A, 0xE8, y, horizon) << 16) |
                               (lerp_u(0x30, 0x78, y, horizon) << 8) |
                               lerp_u(0x60, 0x40, y, horizon))
                            : 0xE87840;
        fill_row(y, 0, W - 1, lerp_rgb(day, dusk, t, tmax));
    }

    int sun_x = 40 + t * 6;
    int sun_y = 42 + t / 3;
    fill_circle(sun_x, sun_y, 22, rgb565(COL_SUN));
    fill_circle(sun_x - 6, sun_y - 4, 8, rgb565(0xFFF0B0));

    int scroll = (t * 5) % 160;
    fill_ellipse(70 - scroll / 2, 200, 100, 55, rgb565(COL_HILL2));
    fill_ellipse(230 - scroll / 3, 210, 120, 48, rgb565(COL_HILL));
    fill_ellipse(40 + (int)(t * 3.5f), 58, 28, 12, rgb565(COL_CLOUD));
    fill_ellipse(56 + (int)(t * 3.5f), 54, 18, 10, rgb565(COL_CLOUD));
    fill_ellipse(210 + t * 2, 70, 24, 10, rgb565(COL_CLOUD));

    float bounce = fabsf(sinf((float)t * 0.55f));
    int bx = 36 + t * 7;
    int by = 168 - (int)(bounce * 70.0f);
    fill_ellipse(bx + 4, 196, 18, 6, rgb565(COL_SHADOW));
    fill_circle(bx, by, 18, rgb565(COL_BALL));
    fill_circle(bx - 6, by - 6, 6, rgb565(COL_BALL_H));

    film_border();
    playhead_bar(frame);
}

static void paint_end(int frame)
{
    int t = frame - SCENE_END;
    uint16_t bg = lerp_rgb(0x2A3060, COL_NAVY, t, CLIP_FRAMES - SCENE_END);
    for (int y = 0; y < H; y++) {
        fill_row(y, 0, W - 1, bg);
    }
    film_border();

    const uint8_t *end[] = {k_e, k_n, k_d};
    int scale = 8;
    int word_w = 3 * (5 * scale) + 2 * scale;
    int x = (W - word_w) / 2;
    int y = (H - 7 * scale) / 2;
    blit_word(x, y, end, 3, scale, rgb565(COL_CARD));
    playhead_bar(frame);
}

static void paint_frame(int frame)
{
    if (frame < TITLE_END) {
        paint_title(frame);
    } else if (frame < SCENE_END) {
        paint_scene(frame);
    } else {
        paint_end(frame);
    }
}

static void maybe_pass(int frame)
{
    if (s_passed || frame != CLIP_FRAMES - 1) {
        return;
    }
    s_passed = true;
    int64_t elapsed = esp_timer_get_time() - s_loop_start_us;
    float fps = elapsed > 0 ? (1000000.0f * CLIP_FRAMES / (float)elapsed) : 0.0f;
    ESP_LOGI(TAG, "clip done  %.1f fps  worst frame %lld us", (double)fps,
             (long long)s_max_us);
    demo_pass("h28");
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("h28", "display");
        return;
    }

    s_fb = heap_caps_malloc(FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_fb == NULL) {
        s_fb = heap_caps_malloc(FB_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_fb == NULL) {
        demo_fail("h28", "fb alloc");
        return;
    }

    memset(&s_dsc, 0, sizeof(s_dsc));
    s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_dsc.header.w = W;
    s_dsc.header.h = H;
    s_dsc.header.stride = W * 2;
    s_dsc.data_size = FB_BYTES;
    s_dsc.data = (const uint8_t *)s_fb;

    if (!board_lvgl_lock(0)) {
        demo_fail("h28", "lvgl lock");
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_NAVY), 0);
    s_img = lv_image_create(scr);
    lv_image_set_src(s_img, &s_dsc);
    lv_obj_set_pos(s_img, 0, 0);
    board_lvgl_unlock();

    ESP_LOGI(TAG, "short clip %d frames @ %d fps", CLIP_FRAMES, FPS);
    s_loop_start_us = esp_timer_get_time();

    while (1) {
        int64_t t0 = esp_timer_get_time();
        paint_frame(s_frame);
        if (board_lvgl_lock(80)) {
            lv_obj_invalidate(s_img);
            lv_refr_now(NULL);
            board_lvgl_unlock();
        }
        int64_t dt = esp_timer_get_time() - t0;
        if (dt > s_max_us) {
            s_max_us = dt;
        }
        maybe_pass(s_frame);

        s_frame++;
        if (s_frame >= CLIP_FRAMES) {
            s_frame = 0;
            s_loop_start_us = esp_timer_get_time();
        }

        int leftover = FRAME_MS - (int)(dt / 1000);
        if (leftover > 0) {
            vTaskDelay(pdMS_TO_TICKS(leftover));
        }
    }
}
