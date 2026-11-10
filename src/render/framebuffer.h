// CPU framebuffer: 32-bit 0xAARRGGBB pixels, row-major, top row first.
// All rendering writes here; SDL only presents the finished image.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rf {

constexpr uint32_t colorRGB(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}
constexpr uint32_t colorARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) | b;
}
inline uint8_t colorR(uint32_t c) { return static_cast<uint8_t>((c >> 16) & 0xFF); }
inline uint8_t colorG(uint32_t c) { return static_cast<uint8_t>((c >> 8) & 0xFF); }
inline uint8_t colorB(uint32_t c) { return static_cast<uint8_t>(c & 0xFF); }
inline uint8_t colorA(uint32_t c) { return static_cast<uint8_t>((c >> 24) & 0xFF); }

// Scales an RGB colour by an integer 0..256 factor.
inline uint32_t shadeRGB(uint32_t c, int factor256) {
    if (factor256 < 0) factor256 = 0;
    if (factor256 > 256) factor256 = 256;
    const uint32_t r = (colorR(c) * static_cast<uint32_t>(factor256)) >> 8;
    const uint32_t g = (colorG(c) * static_cast<uint32_t>(factor256)) >> 8;
    const uint32_t b = (colorB(c) * static_cast<uint32_t>(factor256)) >> 8;
    return colorRGB(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
}

// Straight-alpha blend: src over dst.
inline uint32_t blendRGB(uint32_t dst, uint32_t src, uint8_t alpha) {
    const uint32_t a = alpha;
    const uint32_t ia = 255u - a;
    const uint32_t r = (colorR(src) * a + colorR(dst) * ia) / 255u;
    const uint32_t g = (colorG(src) * a + colorG(dst) * ia) / 255u;
    const uint32_t b = (colorB(src) * a + colorB(dst) * ia) / 255u;
    return colorRGB(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
}

class Framebuffer {
public:
    Framebuffer(int w, int h);

    int width() const { return w_; }
    int height() const { return h_; }
    int pitchPixels() const { return w_; }

    uint32_t* data() { return pixels_.data(); }
    const uint32_t* data() const { return pixels_.data(); }
    uint32_t* row(int y) { return pixels_.data() + static_cast<size_t>(y) * w_; }
    const uint32_t* row(int y) const { return pixels_.data() + static_cast<size_t>(y) * w_; }

    void clear(uint32_t color = 0xFF000000u);
    void putPixel(int x, int y, uint32_t color);
    void blendPixel(int x, int y, uint32_t color, uint8_t alpha);
    void hline(int x0, int x1, int y, uint32_t color);
    void vline(int x, int y0, int y1, uint32_t color);
    void rectFill(int x0, int y0, int x1, int y1, uint32_t color);

    // Writes a 32-bit BMP (bottom-up BGRA). Returns false on I/O failure.
    bool saveBMP(const std::string& path) const;

private:
    int w_;
    int h_;
    std::vector<uint32_t> pixels_;
};

}  // namespace rf
