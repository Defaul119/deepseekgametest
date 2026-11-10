#include "render/texture.h"

#include <algorithm>

#include "core/rng.h"
#include "render/framebuffer.h"

namespace rf {
namespace {

constexpr int kSize = 64;

inline uint32_t rgb(int r, int g, int b) {
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);
    return colorRGB(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
}

inline int channel(uint32_t c, int shift) { return static_cast<int>((c >> shift) & 0xFF); }

inline uint32_t shiftColor(uint32_t c, int dr, int dg, int db) {
    return rgb(channel(c, 16) + dr, channel(c, 8) + dg, channel(c, 0) + db);
}

inline uint32_t mixColor(uint32_t a, uint32_t b, int t) {
    const int r = channel(a, 16) + (channel(b, 16) - channel(a, 16)) * t / 255;
    const int g = channel(a, 8) + (channel(b, 8) - channel(a, 8)) * t / 255;
    const int bl = channel(a, 0) + (channel(b, 0) - channel(a, 0)) * t / 255;
    return rgb(r, g, bl);
}

uint32_t hash2(int x, int y, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return h;
}

int noiseSigned(int x, int y, uint32_t seed, int amp) {
    const int n = static_cast<int>(hash2(x, y, seed) % static_cast<uint32_t>(amp * 2 + 1));
    return n - amp;
}

uint32_t texel(uint32_t c, int x, int y, uint32_t seed, int amp) {
    return shiftColor(c, noiseSigned(x, y, seed, amp), noiseSigned(x + 91, y + 17, seed ^ 0xA5A5u, amp),
                      noiseSigned(x + 13, y + 71, seed ^ 0x5A5Au, amp));
}

std::vector<uint32_t> makeBrick() {
    std::vector<uint32_t> px(static_cast<size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int row = y / 16;
            const int offset = (row & 1) ? 16 : 0;
            const int bx = (x + offset) & 31;
            const int by = y & 15;
            const uint32_t brickSeed = static_cast<uint32_t>((row * 7 + (x + offset) / 32) * 2654435761u);
            const int variation = static_cast<int>(brickSeed % 37) - 18;
            uint32_t c;
            if (bx < 2 || by < 2) {
                c = rgb(40, 31, 28);  // mortar
            } else if (bx > 30 || by > 14) {
                c = shiftColor(rgb(128, 52, 38), variation, variation / 2, 0);
            } else {
                c = rgb(126 + variation, 50 + variation / 2, 38);
                if (by == 2) c = shiftColor(c, 28, 12, 8);
                if (by == 13) c = shiftColor(c, -22, -10, -6);
                if (bx == 2) c = shiftColor(c, 16, 6, 4);
            }
            px[static_cast<size_t>(y) * kSize + x] = texel(c, x, y, 0xBEEF01u, 7);
        }
    }
    return px;
}

std::vector<uint32_t> makeStone() {
    std::vector<uint32_t> px(static_cast<size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int tx = x / 32;
            const int ty = y / 32;
            const int lx = x & 31;
            const int ly = y & 31;
            const uint32_t blockSeed = hash2(tx, ty, 0x51A7u);
            const int variation = static_cast<int>(blockSeed % 29) - 14;
            uint32_t c;
            if (lx < 2 || ly < 2) {
                c = rgb(44, 44, 50);
            } else {
                c = rgb(96 + variation, 96 + variation, 104 + variation);
                if (ly == 2) c = shiftColor(c, 20, 20, 22);
                if (lx == 2) c = shiftColor(c, 12, 12, 14);
                if (ly == 30 || lx == 30) c = shiftColor(c, -18, -18, -18);
            }
            px[static_cast<size_t>(y) * kSize + x] = texel(c, x, y, 0x5107u, 9);
        }
    }
    // A couple of deterministic cracks.
    Rng rng(0xC0FFEEu);
    for (int crack = 0; crack < 6; ++crack) {
        int x = rng.range(4, 60);
        int y = rng.range(4, 60);
        for (int step = 0; step < 14; ++step) {
            if (x >= 0 && x < kSize && y >= 0 && y < kSize) {
                px[static_cast<size_t>(y) * kSize + x] = shiftColor(px[static_cast<size_t>(y) * kSize + x], -46, -46, -42);
            }
            x += rng.range(-1, 1);
            y += rng.range(0, 1);
        }
    }
    return px;
}

std::vector<uint32_t> makeTech() {
    std::vector<uint32_t> px(static_cast<size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int lx = x & 31;
            const int ly = y & 31;
            uint32_t c = rgb(38, 50, 64);
            if (lx < 2 || ly < 2 || lx > 29 || ly > 29) c = rgb(22, 30, 40);
            if (lx == 3 || ly == 3) c = shiftColor(c, 18, 22, 26);
            // Rivets at panel corners.
            const int rx = lx - 5;
            const int ry = ly - 5;
            if ((rx == 0 || rx == 1) && (ry == 0 || ry == 1)) c = rgb(150, 158, 168);
            if ((lx == 26 || lx == 27) && (ly == 26 || ly == 27)) c = rgb(130, 138, 148);
            px[static_cast<size_t>(y) * kSize + x] = texel(c, x, y, 0x7EC4u, 5);
        }
    }
    // Glowing indicator strip across the lower middle.
    for (int x = 0; x < kSize; ++x) {
        for (int y = 34; y < 40; ++y) {
            const bool edge = (y == 34 || y == 39);
            uint32_t c = edge ? rgb(20, 30, 36) : rgb(92, 200, 210);
            if (!edge && (x % 8 == 0)) c = rgb(150, 230, 235);
            px[static_cast<size_t>(y) * kSize + x] = c;
        }
    }
    return px;
}

std::vector<uint32_t> makeDoor() {
    std::vector<uint32_t> px(static_cast<size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int ridge = (x / 8) & 1;
            uint32_t c = rgb(ridge ? 118 : 100, ridge ? 96 : 80, ridge ? 62 : 50);
            if (x == 31 || x == 32) c = rgb(30, 26, 22);
            if (x == 3 || x == 60) c = rgb(60, 50, 40);
            if (y < 4) c = rgb(55, 45, 32);
            if ((x == 12 || x == 51) && (y == 8 || y == 9)) c = rgb(170, 165, 150);
            if ((x == 12 || x == 51) && (y == 54 || y == 55)) c = rgb(170, 165, 150);
            // Hazard stripes at the bottom.
            if (y >= 52) {
                const int stripe = ((x + y) / 6) & 1;
                c = stripe ? rgb(196, 172, 56) : rgb(34, 30, 26);
                if (y == 52) c = shiftColor(c, -20, -20, -20);
            }
            px[static_cast<size_t>(y) * kSize + x] = texel(c, x, y, 0xD001u, 6);
        }
    }
    return px;
}

std::vector<uint32_t> makeMetal() {
    std::vector<uint32_t> px(static_cast<size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            uint32_t c = rgb(78, 80, 88);
            const int lx = x & 15;
            const int ly = y & 15;
            if (lx == 0 || ly == 0) c = rgb(48, 50, 58);
            if (lx == 1 || ly == 1) c = rgb(104, 106, 116);
            if ((lx == 8 || lx == 9) && (ly == 8 || ly == 9)) c = rgb(150, 150, 158);
            px[static_cast<size_t>(y) * kSize + x] = texel(c, x, y, 0x3E7A1u, 6);
        }
    }
    return px;
}

std::vector<uint32_t> makeGrate() {
    std::vector<uint32_t> px(static_cast<size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int lx = x & 15;
            const int ly = y & 15;
            uint32_t c = rgb(18, 18, 22);
            if (lx >= 5 && lx <= 10) {
                c = (lx == 5) ? rgb(92, 92, 100) : ((lx == 10) ? rgb(48, 48, 54) : rgb(74, 74, 82));
                if (ly == 0 || ly == 15) c = rgb(110, 110, 118);
            } else if (ly == 0) {
                c = rgb(40, 40, 46);
            }
            px[static_cast<size_t>(y) * kSize + x] = texel(c, x, y, 0x6A7E1u, 4);
        }
    }
    return px;
}

std::vector<uint32_t> makeStep() {
    std::vector<uint32_t> px(static_cast<size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            uint32_t c = rgb(94, 78, 58);
            const int ly = y & 15;
            if (ly == 0) c = rgb(140, 120, 92);
            if (ly == 15) c = rgb(56, 46, 34);
            if ((x & 31) == 0) c = shiftColor(c, -20, -16, -12);
            px[static_cast<size_t>(y) * kSize + x] = texel(c, x, y, 0x57E9u, 8);
        }
    }
    return px;
}

std::vector<uint32_t> makeWood() {
    std::vector<uint32_t> px(static_cast<size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int plank = x & 15;
            uint32_t c = rgb(112, 72, 42);
            if (plank == 0) c = rgb(50, 32, 20);
            if (plank == 1) c = rgb(138, 92, 56);
            const int grain = ((x * 13 + y * 7) % 23);
            if (grain < 3) c = shiftColor(c, -14, -10, -6);
            px[static_cast<size_t>(y) * kSize + x] = texel(c, x, y, 0x0DDu, 5);
        }
    }
    return px;
}

std::vector<uint32_t> makeFloor() {
    std::vector<uint32_t> px(static_cast<size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int lx = x & 31;
            const int ly = y & 31;
            const uint32_t tileSeed = hash2(x / 32, y / 32, 0xF100u);
            const int variation = static_cast<int>(tileSeed % 17) - 8;
            uint32_t c = rgb(82 + variation, 88 + variation, 98 + variation);
            if (lx < 2 || ly < 2) c = rgb(38, 40, 48);
            if (lx == 2 || ly == 2) c = shiftColor(c, 12, 12, 14);
            px[static_cast<size_t>(y) * kSize + x] = texel(c, x, y, 0xF107u, 6);
        }
    }
    return px;
}

std::vector<uint32_t> makeCeil() {
    std::vector<uint32_t> px(static_cast<size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int lx = x & 31;
            const int ly = y & 31;
            uint32_t c = rgb(58, 58, 66);
            if (lx < 2 || ly < 2) c = rgb(34, 34, 40);
            if (lx == 16 || ly == 16) c = rgb(48, 48, 56);
            if ((lx == 6 || lx == 25) && (ly == 6 || ly == 25)) c = rgb(98, 98, 108);
            px[static_cast<size_t>(y) * kSize + x] = texel(c, x, y, 0xCE11u, 4);
        }
    }
    return px;
}

std::vector<uint32_t> makeTile() {
    std::vector<uint32_t> px(static_cast<size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int lx = x & 15;
            const int ly = y & 15;
            const uint32_t tileSeed = hash2(x / 16, y / 16, 0x71E5u);
            const int variation = static_cast<int>(tileSeed % 21) - 10;
            uint32_t c = rgb(56 + variation, 98 + variation, 92 + variation);
            if (lx < 1 || ly < 1) c = rgb(28, 46, 44);
            if (lx == 1 || ly == 1) c = shiftColor(c, 14, 20, 18);
            px[static_cast<size_t>(y) * kSize + x] = texel(c, x, y, 0x71E7u, 5);
        }
    }
    return px;
}

std::vector<uint32_t> makeCarpet() {
    std::vector<uint32_t> px(static_cast<size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            uint32_t c = rgb(92, 30, 36);
            const int cx = x & 15;
            const int cy = y & 15;
            if ((cx + cy) % 16 == 0 || (cx - cy + 16) % 16 == 0) c = rgb(132, 48, 54);
            if (cx == 0 || cy == 0) c = rgb(58, 20, 26);
            px[static_cast<size_t>(y) * kSize + x] = texel(c, x, y, 0xCA2u, 4);
        }
    }
    return px;
}

}  // namespace

TextureSet::TextureSet() {
    add("BRICK", kSize, kSize, makeBrick());
    add("STONE", kSize, kSize, makeStone());
    add("TECH", kSize, kSize, makeTech());
    add("DOOR", kSize, kSize, makeDoor());
    add("METAL", kSize, kSize, makeMetal());
    add("GRATE", kSize, kSize, makeGrate());
    add("STEP", kSize, kSize, makeStep());
    add("WOOD", kSize, kSize, makeWood());
    add("FLOOR", kSize, kSize, makeFloor());
    add("CEIL", kSize, kSize, makeCeil());
    add("TILE", kSize, kSize, makeTile());
    add("CARPET", kSize, kSize, makeCarpet());
}

int TextureSet::add(std::string name, int w, int h, std::vector<uint32_t> pixels) {
    Texture t;
    t.name = std::move(name);
    t.w = w;
    t.h = h;
    t.pixels = std::move(pixels);
    const int id = static_cast<int>(textures_.size());
    index_[t.name] = id;
    textures_.push_back(std::move(t));
    return id;
}

int TextureSet::find(std::string_view name) const {
    auto it = index_.find(std::string(name));
    return it == index_.end() ? -1 : it->second;
}

const Texture& TextureSet::get(int id) const { return textures_.at(static_cast<size_t>(id)); }

const std::string& TextureSet::name(int id) const { return textures_.at(static_cast<size_t>(id)).name; }

int shadeFactor(int light) {
    static int table[256];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < 256; ++i) {
            table[i] = 48 + (i * 207) / 255;  // 48 (very dark) .. 255 (full)
        }
        built = true;
    }
    if (light < 0) light = 0;
    if (light > 255) light = 255;
    return table[light];
}

}  // namespace rf
