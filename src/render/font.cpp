#include "render/font.h"

#include "render/font_data.h"

namespace rf::Font {

namespace {

uint8_t glyphRow(unsigned char c, int row) {
    if (c >= 128) c = '?';
    return font8x8::kGlyphs[c][row];
}

}  // namespace

void drawText(Framebuffer& fb, int x, int y, std::string_view text, uint32_t color, int scale) {
    if (scale < 1) scale = 1;
    int penX = x;
    for (char chRaw : text) {
        const unsigned char ch = static_cast<unsigned char>(chRaw);
        if (ch == '\n') {
            penX = x;
            y += kGlyphHeight * scale;
            continue;
        }
        for (int row = 0; row < 8; ++row) {
            const uint8_t bits = glyphRow(ch, row);
            for (int col = 0; col < 8; ++col) {
                if (bits & (1u << col)) {
                    fb.rectFill(penX + col * scale, y + row * scale,
                                penX + col * scale + scale - 1, y + row * scale + scale - 1,
                                color);
                }
            }
        }
        penX += kGlyphWidth * scale;
    }
}

void drawTextShadow(Framebuffer& fb, int x, int y, std::string_view text, uint32_t color,
                    uint32_t shadow, int scale) {
    drawText(fb, x + scale, y + scale, text, shadow, scale);
    drawText(fb, x, y, text, color, scale);
}

int textWidth(std::string_view text, int scale) {
    if (scale < 1) scale = 1;
    int best = 0;
    int current = 0;
    for (char ch : text) {
        if (ch == '\n') {
            if (current > best) best = current;
            current = 0;
        } else {
            current += kGlyphWidth * scale;
        }
    }
    return current > best ? current : best;
}

int textHeight(int scale) {
    if (scale < 1) scale = 1;
    return kGlyphHeight * scale;
}

}  // namespace rf::Font
