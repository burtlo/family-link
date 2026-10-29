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

namespace product_theme
{

struct Dark {
    static constexpr uint32_t kSurfaceRgb24 = PRODUCT_THEME_DARK_SURFACE_RGB24;
    static constexpr uint32_t kSurfaceUpperRgb24 = PRODUCT_THEME_DARK_SURFACE_UPPER_RGB24;
    static constexpr uint32_t kSurfaceLowerRgb24 = PRODUCT_THEME_DARK_SURFACE_LOWER_RGB24;
};

} /* namespace product_theme */
