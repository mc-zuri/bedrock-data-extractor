#pragma once
// The block type registry, for World.cpp (finding it) and Blocks.cpp (listing it); exporters use game/Blocks.h.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bdg::game {

// The populated std::map<HashedString, BlockType owner> of every block type, by the first route this
// build's bindings support: Level member, Level getter, then the static map. 0 when none validates (yet).
std::uintptr_t blockTypeMap(std::uintptr_t level);

// In-order walk of that map (the order runtime block ids are assigned in), by its keys: the names, lowercased before
// 1.18.30. Returns the types visited.
int forEachBlockType(std::uintptr_t map, std::function<void(std::string const& key, std::uintptr_t blockType)> const& visit);

// A block type's states (Block*), in permutation order.
std::vector<std::uintptr_t> permutations(std::uintptr_t blockType);

// Its default state (BlockLegacy::mDefaultState, the Block* right after mBlockPermutations on every build); 0 when unreadable.
std::uintptr_t defaultState(std::uintptr_t blockType);

}
