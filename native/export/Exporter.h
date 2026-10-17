#pragma once
// What the agent exports: one function per kind of data, each writing its own file into BDG_OUT, all of them listed
// in `exporters` (agent/Agent.cpp). The extractor reads, checks and writes each into bedrock-data (src/blocks.ts).
// Exporters read the game only through game/World.h and game/Blocks.h, which are the same on every build.

#include "game/Blocks.h"
#include "game/World.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bdg::exporter {

struct Context {
    game::World                  world;
    std::vector<game::BlockType> blockTypes;  // in runtime id order
    std::string                  out;         // BDG_OUT
};

struct Result {
    bool        ok = false;
    std::string message;  // what it wrote ("14196 states"), or why it failed
};

using Exporter = Result (*)(Context const&);

// <out>/block_palette.nbt: big-endian { blocks: [<each state's CompoundTag>] }, every state in runtime id order.
Result exportPalette(Context const& context);

// <out>/block_shapes.nbt: big-endian { blocks: [{ name, states: [{ collision: [[minX, minY, minZ, maxX, maxY, maxZ], ...],
// visual: [6 floats], ui: [6 floats], outline: [6 floats], camera?: [] or [[6 floats]] }] }] }, every block type in
// runtime id order with, for each of its states, its shapes (camera from 1.20.30).
Result exportShapes(Context const& context);

// Through a temporary file moved over `name` in `out`: never half a file.
bool writeFile(Context const& context, std::string const& name, std::vector<std::uint8_t> const& bytes);

}
