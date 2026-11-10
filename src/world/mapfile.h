// Text level format (.rmap). Version 1 is line based:
//
//   version 1
//   sector  <id> <floor> <ceil> <floorTex> <ceilTex> <light> <tag> <special>
//   vertex  <id> <x> <y>
//   linedef <id> <v1> <v2> <front> <back| -1> <mid|- > <up|- > <low|-> <flags> <special> <tag>
//   thing   <x> <y> <angleDegrees> <type>
//
// Coordinates and heights are in map units (1 unit = FRACUNIT), and may be
// fractional. '#' and '//' start comments. Records may appear in any order;
// ids must be dense starting at zero. Texture "-" means none.
#pragma once

#include <string>

#include "render/texture.h"
#include "world/world.h"

namespace rf {

bool loadMap(const std::string& path, const TextureSet& textures, World& out, std::string* error);
bool saveMap(const std::string& path, const World& world, const TextureSet& textures,
             std::string* error);

// Parses from a string (used by tests and the built-in fallback map).
bool loadMapText(const std::string& text, const TextureSet& textures, World& out,
                 std::string* error);

}  // namespace rf
