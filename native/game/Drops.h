#pragma once
// What each block state drops, as the game's own drop code answers: the same on every build.

#include "game/Blocks.h"
#include "game/Items.h"
#include "game/World.h"

#include <string>
#include <vector>

namespace bdg::game {

// One item a state can drop when a player mines it with an empty hand: over `samples` draws of the game's drop code
// (each type's Random seeded by its name, so the same code gives the same answers), how many in a stack and how often.
struct Drop {
    std::string item;                // "minecraft:cobblestone"
    int         aux;                 // the stack's own data value
    int         data;                // its data value as ItemStackBase::getAuxValue reads it: of the block it carries, if it carries one
    int         minCount, maxCount;  // the fewest and most in a draw that dropped it
    int         hits;                // draws that dropped it
    bool        operator==(Drop const&) const = default;
};

struct StateDrops {
    std::vector<Drop> drops;  // by item, then data values
    int               samples;
    bool              operator==(StateDrops const&) const = default;
};

// A state's map colour, as Block::getMapColor answers where the state is set: mce::Color, each 0 - 1
struct MapColor {
    float r, g, b, a;
    bool  operator==(MapColor const&) const = default;
};

struct AllDrops {
    std::vector<std::vector<StateDrops>> types;              // per block type in the order asked about, per state in its order
    std::vector<std::vector<MapColor>>   mapColors;          // the same way
    std::string                          error;              // why the game's drop code was not found, or a call faulted; then there are none
    bool                                 notLoaded = false;  // the error is that no chunk is loaded where blocks are set yet: a later tick may have one
};

AllDrops drops(World const& world, ItemTypes const& items, std::vector<BlockType> const& types);

}
