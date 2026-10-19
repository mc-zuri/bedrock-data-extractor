#pragma once
// The biomes and their numeric ids, the same on every build. They are found and read by content (Biomes.cpp): no
// binding says where the biome registry is or how a Biome is laid out.

#include "game/World.h"

#include <string>
#include <vector>

namespace bdg::game {

struct Biome {
    std::string name;  // "plains": without minecraft:, as the registry names it
    int         id;    // the numeric id a chunk's biomes are saved and sent by: ocean 0, plains 1
    // the Biome objects that read this id: the live one, and any copy that still reads as it
    std::vector<std::uintptr_t> objects;
};

struct Biomes {
    std::vector<Biome> biomes;  // by name
    std::string        error;   // why the registry does not read right; then there are none
};

Biomes biomes(World const& world);

}
