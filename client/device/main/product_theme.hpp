#pragma once

#include <cstdint>

/*
 * Device UI color tokens. Runtime theme switching will select among these sets.
 * Authoritative names and usage: docs/product/device-ui/themes/THEME-DARK.md
 */

/* THEME-DARK — fl-color-surface (full canvas / shell backdrop) */
#define PRODUCT_THEME_DARK_SURFACE_RGB24 UINT32_C(0x101418)

/* THEME-DARK — split-pane slot chrome (LAYOUT-SPLIT-PANE two-pane treatment) */
#define PRODUCT_THEME_DARK_SURFACE_UPPER_RGB24 UINT32_C(0x101418)
#define PRODUCT_THEME_DARK_SURFACE_LOWER_RGB24 UINT32_C(0x242C34)

/* THEME-DARK — type and accent on surface */
#define PRODUCT_THEME_DARK_TEXT_PRIMARY_RGB24 UINT32_C(0xE8F0E8)
#define PRODUCT_THEME_DARK_TEXT_SECONDARY_RGB24 UINT32_C(0xA8B0B8)
#define PRODUCT_THEME_DARK_ACCENT_RGB24 UINT32_C(0xE8C040)

/* THEME-DARK — record sketch and palette chrome */
#define PRODUCT_THEME_DARK_SKETCH_CANVAS_RGB24 UINT32_C(0x000000)
#define PRODUCT_THEME_DARK_SWATCH_RING_RGB24 UINT32_C(0xB8BEC6)

/* THEME-DARK — optional non-focused peek fill */
#define PRODUCT_THEME_DARK_CARD_PEEK_RGB24 UINT32_C(0x242C34)

/* THEME-DARK — as-built chrome not previously named in the placement map */
#define PRODUCT_THEME_DARK_HEADING_RGB24 UINT32_C(0xFFFFFF)
#define PRODUCT_THEME_DARK_SUBHEADING_RGB24 UINT32_C(0xB7CBD7)
#define PRODUCT_THEME_DARK_AVATAR_DEFAULT_RGB24 UINT32_C(0xF4F4F4)
#define PRODUCT_THEME_DARK_PIN_KEYS_RGB24 UINT32_C(0xD7E2EA)
#define PRODUCT_THEME_DARK_RECORD_RING_RGB24 UINT32_C(0xE53935)

/* fl-color-text-on-card is per FOUNDATION-CARD-BACKGROUNDS, not a single theme swatch. */

namespace product_theme
{

struct Dark {
    static constexpr uint32_t kSurfaceRgb24 = PRODUCT_THEME_DARK_SURFACE_RGB24;
    static constexpr uint32_t kSurfaceUpperRgb24 = PRODUCT_THEME_DARK_SURFACE_UPPER_RGB24;
    static constexpr uint32_t kSurfaceLowerRgb24 = PRODUCT_THEME_DARK_SURFACE_LOWER_RGB24;
    static constexpr uint32_t kTextPrimaryRgb24 = PRODUCT_THEME_DARK_TEXT_PRIMARY_RGB24;
    static constexpr uint32_t kTextSecondaryRgb24 = PRODUCT_THEME_DARK_TEXT_SECONDARY_RGB24;
    static constexpr uint32_t kAccentRgb24 = PRODUCT_THEME_DARK_ACCENT_RGB24;
    static constexpr uint32_t kSketchCanvasRgb24 = PRODUCT_THEME_DARK_SKETCH_CANVAS_RGB24;
    static constexpr uint32_t kSwatchRingRgb24 = PRODUCT_THEME_DARK_SWATCH_RING_RGB24;
    static constexpr uint32_t kCardPeekRgb24 = PRODUCT_THEME_DARK_CARD_PEEK_RGB24;
    static constexpr uint32_t kHeadingRgb24 = PRODUCT_THEME_DARK_HEADING_RGB24;
    static constexpr uint32_t kSubheadingRgb24 = PRODUCT_THEME_DARK_SUBHEADING_RGB24;
    static constexpr uint32_t kAvatarDefaultRgb24 = PRODUCT_THEME_DARK_AVATAR_DEFAULT_RGB24;
    static constexpr uint32_t kPinKeysRgb24 = PRODUCT_THEME_DARK_PIN_KEYS_RGB24;
    static constexpr uint32_t kRecordRingRgb24 = PRODUCT_THEME_DARK_RECORD_RING_RGB24;
};

} /* namespace product_theme */
