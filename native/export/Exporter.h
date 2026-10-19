#pragma once
// What the agent exports: one function per kind of data, each writing its own file into BDG_OUT, all of them listed
// in `exporters` (agent/Agent.cpp). The extractor reads, checks and writes each into bedrock-data (src/blocks.ts).
// Exporters read the game only through game/World.h, game/Blocks.h, game/Items.h, game/Dig.h, game/Drops.h,
// game/Enchants.h, game/Biomes.h, game/Effects.h, game/Enums.h and game/Tints.h, which are the same on every build.

#include "game/Biomes.h"
#include "game/Blocks.h"
#include "game/Effects.h"
#include "game/Enchants.h"
#include "game/Enums.h"
#include "game/Items.h"
#include "game/Tints.h"
#include "game/World.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bdg::exporter {

struct Context {
    game::World                  world;
    std::vector<game::BlockType> blockTypes;  // in runtime id order
    game::ItemTypes              items;
    std::string                  out;  // BDG_OUT
};

struct Result {
    bool        ok = false;
    std::string message;  // what it wrote ("14196 states"), or why it failed
    // a failure a later tick may not have: no chunk is loaded yet where the drops are drawn, or what it reads is found
    // among objects the game is still changing
    bool again = false;
};

using Exporter = Result (*)(Context const&);

// <out>/block_palette.nbt: big-endian { blocks: [<each state's CompoundTag>] }, every state in runtime id order.
Result exportPalette(Context const& context);

// <out>/block_shapes.nbt: big-endian { blocks: [{ name, states: [{ collision: [[minX, minY, minZ, maxX, maxY, maxZ], ...],
// visual: [6 floats], ui: [6 floats], outline: [6 floats], camera?: [] or [[6 floats]] }] }] }, every block type in
// runtime id order with, for each of its states, its shapes (camera from 1.20.30).
Result exportShapes(Context const& context);

// <out>/blocks.nbt: big-endian { blocks: [{ name, id, defaultState, descriptionId, requiresCorrectTool, tags, translucency,
// states: [{ data, hardness, explosionResistance, friction, flameOdds, burnOdds, lightEmission,
// lightDampening, canContainLiquid, destroySpeeds: [{ item, speed }], harvestedBy: [item], samples, drops: [{ item, aux, data, minCount,
// maxCount, hits }] }] }] }, every block type in runtime id order with what the game says of each of its states: its
// facts, what every item does to it (the speeds that are not 1), and what it drops when a player mines it with an
// empty hand over `samples` draws (0: a data-driven type, which has no drop code).
Result exportBlocks(Context const& context);

// <out>/items.nbt: big-endian { items: [{ name, id, descriptionId, descriptionName, maxStackSize, maxDamage, enchantSlots,
// tags, repairItems, nutrition?, saturationModifier? }], aliases: [{ name, item }], complexAliases: [{ name, items }] },
// every registered item by name (a food with its legacy food component's values, where the build keeps one), and the old
// names the game still reads.
Result exportItems(Context const& context);

// <out>/enchantments.nbt: big-endian { enchantments: [{ id, name, descriptionId, frequency, tradeable, primarySlots,
// secondarySlots, minLevel, maxLevel, minCost, maxCost, incompatible, treasureOnly, discoverable }] }, every enchantment by
// id from 0, with its costs at levels 1 to its max level (at least 2) and the ids of those it does not go with.
Result exportEnchantments(Context const& context);

// <out>/biomes.nbt: big-endian { biomes: [{ name, id }] }, every biome of the registry by name with its numeric id.
Result exportBiomes(Context const& context);

// <out>/effects.nbt: big-endian { effects: [{ id, name, descriptionId, harmful }] }, every mob effect by id from 1.
Result exportEffects(Context const& context);

// <out>/enums.nbt: big-endian { sounds, particles, containers, instruments, mapDecorations, containerNames,
// explorationMarkers: [{ id, name }] }: the names the game gives its sound events, particle types, container types, note
// block instruments, map decorations and item stack request containers, by id, and the decoration types exploration maps
// mark (game/Enums.h).
Result exportEnums(Context const& context);

// <out>/tints.nbt: big-endian { route, biomes: [{ name, grass, foliage, water }], redstone: [int], constant: [{ name,
// color }] }: the colours the server draws its maps with (game/Tints.h): each biome's, by name; the redstone wire's in
// each of its states, in state order; the block types it tints with one colour, in runtime id order.
Result exportTints(Context const& context);

// Through a temporary file moved over `name` in `out`: never half a file.
bool writeFile(Context const& context, std::string const& name, std::vector<std::uint8_t> const& bytes);

}
