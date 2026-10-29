#pragma once

/*
 * Fixed ESP32-S3-BOX-3 desk framebuffer (landscape RGB565).
 * See docs/product/device-ui/foundations/FOUNDATION-DEVICE-CANVAS.md
 */

#define PRODUCT_CANVAS_WIDTH 320
#define PRODUCT_CANVAS_HEIGHT 240

namespace product_canvas
{

constexpr int kWidth = PRODUCT_CANVAS_WIDTH;
constexpr int kHeight = PRODUCT_CANVAS_HEIGHT;

} /* namespace product_canvas */
