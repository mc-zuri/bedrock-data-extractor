#pragma once
// The colours the server tints with, as it draws its maps (a dedicated server renders nothing else: the palettes the
// client tints its blocks from are never filled there): each biome's grass, foliage and water, the redstone wire's in each
// of its states, and the block types it tints with one colour wherever they are. The game's own functions answer, found
// by their code (Tints.cpp), the same on every build.

#include "game/Biomes.h"
#include "game/Blocks.h"

#include <string>
#include <vector>

namespace bdg::game {

struct BiomeTint {
    std::string name;
    int         grass, foliage, water;  // 0xRRGGBB
};

struct BlockTint {
    std::string name;   // "minecraft:birch_leaves"
    int         color;  // 0xRRGGBB
};

struct Tints {
    std::vector<BiomeTint> biomes;    // by name
    std::vector<int>       redstone;  // the redstone wire's colour in each of its states, in state order
    std::vector<BlockTint> constant;  // in runtime id order
    std::string            route;     // how the functions were found: "biome" (to 1.21.50), "samplers" (from 1.21.60)
    std::string            error;     // why they were not; then there are none
};

Tints tints(Biomes const& biomes, std::vector<BlockType> const& types);

}
