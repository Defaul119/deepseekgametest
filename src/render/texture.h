// Procedurally generated wall/flat textures. No external image assets are
// required; every texture is authored deterministically in code.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rf {

struct Texture {
    std::string name;
    int w = 0;
    int h = 0;
    std::vector<uint32_t> pixels;

    uint32_t sample(int x, int y) const {
        // All generated textures have power-of-two dimensions.
        x &= (w - 1);
        y &= (h - 1);
        return pixels[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
    }
};

class TextureSet {
public:
    TextureSet();

    int find(std::string_view name) const;
    const Texture& get(int id) const;
    const std::string& name(int id) const;
    int count() const { return static_cast<int>(textures_.size()); }

private:
    int add(std::string name, int w, int h, std::vector<uint32_t> pixels);

    std::vector<Texture> textures_;
    std::unordered_map<std::string, int> index_;
};

// Maps a sector light level (0..255) to a 0..256 shading multiplier.
// Kept as a lookup table so the lighting hot path is one array read.
int shadeFactor(int light);

}  // namespace rf
