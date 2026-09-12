#ifndef V1_TYPES_H
#define V1_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define V1_USER_MAX       8
#define V1_MSG_MAX        16
#define V1_ENTRY_MAX      8
#define V1_JSON_CAP       4096
#define V1_HANGOUT_NVS_MAX 1536
#define V1_AVATAR_SLOTS   12
#define V1_ACCENT_COUNT   10
#define V1_RIBBON_H       20
#define V1_ROSTER_FACE_SZ 36
#define V1_CONNECT_ICON_SCALE 150

#define V1_LCD_W               320
#define V1_CONTENT_H           (240 - 2 * V1_RIBBON_H)
#define V1_CAROUSEL_HEADER_H   (V1_CONTENT_H / 3)
#define V1_CAROUSEL_HEADER_Y   (V1_RIBBON_H)
#define V1_SCROLL_CARD_W       132
#define V1_SCROLL_CARD_H       (V1_CONTENT_H - V1_CAROUSEL_HEADER_H)
#define V1_SCROLL_CARD_GAP     12
#define V1_SCROLL_CARD_Y       (V1_RIBBON_H + V1_CAROUSEL_HEADER_H)
#define V1_CARD_RADIUS         8
#define V1_CARD_PORTRAIT       22
#define V1_CAROUSEL_PLAY_PAD   8
#define V1_CAROUSEL_SENDER_FONT 24  /* web twin uses 23px; LVGL has 24 */
#define V1_CAROUSEL_PLAY       (V1_CAROUSEL_HEADER_H - 2 * V1_CAROUSEL_PLAY_PAD)
#define V1_CAROUSEL_DISK       V1_CAROUSEL_PLAY
#define V1_CARD_PLAY_ICON      V1_CAROUSEL_PLAY
#define V1_SAMPLE_RATE         16000
#define V1_PLAYBACK_BUF_CAP    (320 * 1024)
#define V1_ROOMVOL_FIRST       78
#define V1_ROOMVOL_STEP        2
#define V1_ROOMVOL_MAX         100
#define V1_ROOMVOL_ON          (((V1_ROOMVOL_MAX - V1_ROOMVOL_FIRST) / V1_ROOMVOL_STEP) + 1)
#define V1_CIRCLE_DEBOUNCE_MS  400
#define V1_UI_SLEEP_BG         0x080A0C
#define V1_BRIGHT_NORM         80
#define V1_BRIGHT_DIM          32
#define V1_BRIGHT_SLEEP        4
#define V1_BRIGHT_SLEEP_PEAK   6
#define V1_SLEEP_FADE_MS       1500
#define V1_SLEEP_BREATHE_MS    5000
#define V1_SLEEP_HINT_MS       25000
#define V1_SLEEP_HINT_PULSE_MS 900
#define V1_CARD_GRAD_N         3
#define V1_TALK_PEAK           256
#define V1_CHIRP_MS            160
#define V1_CHIRP_DRAIN_MS      60
#define V1_CHIRP_VOL           70
#define V1_CHIRP_WRITE         1024

#define V1_UI_BG          0x101418
#define V1_UI_CARD        0x1C2228
#define V1_UI_CARD_PRESS  0x242C34
#define V1_UI_TEXT        0xF0F4F0
#define V1_UI_TEXT_MUT    0xB0B8C0
#define V1_UI_TEXT_DIM    0x788088
#define V1_UI_ACCENT      0xE8C040
#define V1_UI_ERROR       0xE85A5A

typedef enum {
    ST_CONNECTING,
    ST_WIFI_ERR,
    ST_ROSTER,
    ST_PIN,
    ST_CAROUSEL,
    ST_SETTINGS,
    ST_PICK,
    ST_RECORD,
} state_t;

typedef struct {
    char id[16];
    char name[24];
    uint8_t avatar_slot;
    uint32_t accent;
} user_t;

typedef struct {
    int seq;
    char from_label[24];
    bool read;
    int position_ms;
    int duration_ms;
} msg_t;

#endif /* V1_TYPES_H */
