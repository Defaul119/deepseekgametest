// 8x8 bitmap text drawing into the software framebuffer.
#pragma once

#include <string_view>

#include "render/framebuffer.h"

namespace rf::Font {

constexpr int kGlyphWidth = 8;
constexpr int kGlyphHeight = 8;

void drawText(Framebuffer& fb, int x, int y, std::string_view text, uint32_t color,
              int scale = 1);

// Draws with a one-pixel (scaled) drop shadow.
void drawTextShadow(Framebuffer& fb, int x, int y, std::string_view text, uint32_t color,
                    uint32_t shadow = 0xFF000000u, int scale = 1);

int textWidth(std::string_view text, int scale = 1);
int textHeight(int scale = 1);

}  // namespace rf::Font
