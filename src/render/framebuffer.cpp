#include "render/framebuffer.h"

#include <algorithm>
#include <fstream>

namespace rf {

Framebuffer::Framebuffer(int w, int h)
    : w_(w), h_(h), pixels_(static_cast<size_t>(w) * static_cast<size_t>(h), 0xFF000000u) {}

void Framebuffer::clear(uint32_t color) {
    std::fill(pixels_.begin(), pixels_.end(), color);
}

void Framebuffer::putPixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    pixels_[static_cast<size_t>(y) * w_ + x] = color;
}

void Framebuffer::blendPixel(int x, int y, uint32_t color, uint8_t alpha) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    uint32_t& dst = pixels_[static_cast<size_t>(y) * w_ + x];
    dst = blendRGB(dst, color, alpha);
}

void Framebuffer::hline(int x0, int x1, int y, uint32_t color) {
    if (y < 0 || y >= h_) return;
    if (x0 > x1) std::swap(x0, x1);
    x0 = std::max(0, x0);
    x1 = std::min(w_ - 1, x1);
    if (x0 > x1) return;
    uint32_t* p = row(y);
    for (int x = x0; x <= x1; ++x) p[x] = color;
}

void Framebuffer::vline(int x, int y0, int y1, uint32_t color) {
    if (x < 0 || x >= w_) return;
    if (y0 > y1) std::swap(y0, y1);
    y0 = std::max(0, y0);
    y1 = std::min(h_ - 1, y1);
    for (int y = y0; y <= y1; ++y) pixels_[static_cast<size_t>(y) * w_ + x] = color;
}

void Framebuffer::rectFill(int x0, int y0, int x1, int y1, uint32_t color) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(w_ - 1, x1);
    y1 = std::min(h_ - 1, y1);
    if (x0 > x1 || y0 > y1) return;
    for (int y = y0; y <= y1; ++y) {
        uint32_t* p = row(y);
        for (int x = x0; x <= x1; ++x) p[x] = color;
    }
}

bool Framebuffer::saveBMP(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    const uint32_t rowBytes = static_cast<uint32_t>(w_) * 4u;
    const uint32_t imageSize = rowBytes * static_cast<uint32_t>(h_);
    const uint32_t fileSize = 54u + imageSize;

    auto put16 = [&out](uint32_t v) {
        out.put(static_cast<char>(v & 0xFF));
        out.put(static_cast<char>((v >> 8) & 0xFF));
    };
    auto put32 = [&out](uint32_t v) {
        out.put(static_cast<char>(v & 0xFF));
        out.put(static_cast<char>((v >> 8) & 0xFF));
        out.put(static_cast<char>((v >> 16) & 0xFF));
        out.put(static_cast<char>((v >> 24) & 0xFF));
    };

    out.put('B');
    out.put('M');
    put32(fileSize);
    put16(0);
    put16(0);
    put32(54);
    put32(40);                 // DIB header size
    put32(static_cast<uint32_t>(w_));
    put32(static_cast<uint32_t>(h_));
    put16(1);                  // planes
    put16(32);                 // bits per pixel
    put32(0);                  // BI_RGB
    put32(imageSize);
    put32(2835);               // 72 dpi
    put32(2835);
    put32(0);
    put32(0);

    // BMP rows are stored bottom-up. Pixels are ARGB in memory order BGRA.
    for (int y = h_ - 1; y >= 0; --y) {
        out.write(reinterpret_cast<const char*>(row(y)), static_cast<std::streamsize>(rowBytes));
    }
    return out.good();
}

}  // namespace rf
