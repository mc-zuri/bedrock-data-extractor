#pragma once
// The block type registry, for World.cpp (finding it) and Blocks.cpp (listing it); exporters use game/Blocks.h.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace bdg::game {

struct BlockType;

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

// Its drop strategy (BlockLegacy::mResourceDropsStrategy, an IResourceDropsStrategy owned 0x88 after mBlockPermutations
// from 1.20.71 where no binding says otherwise); 0 when it has none.
std::uintptr_t dropStrategy(std::uintptr_t blockType);

// Block::mData, a state's data value (what ItemStackBase::getAuxValue reads of a stack that carries it): the one uint16
// offset where the states of the types with one numbered property read their number in order (wheat's growth,
// farmland's moisture, cake's bites, the composter's fill, nether wart's age); -1 when none or several.
std::int64_t blockDataOffset(std::vector<BlockType> const& types);

}
