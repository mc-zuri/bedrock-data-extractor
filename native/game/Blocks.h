#pragma once
// Block types and their states, the same on every build.

#include "core/Nbt.h"
#include "game/World.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bdg::game {

struct BlockType {
    std::string                 name;          // as the game names it: "minecraft:pistonArmCollision"
    std::uintptr_t              address;       // BlockLegacy*
    std::vector<std::uintptr_t> states;        // Block*, in permutation order: consecutive runtime ids
    std::size_t                 defaultState;  // the game's, as an index into states
};

struct BlockTypes {
    std::vector<BlockType> types;  // in runtime id order
    std::string            error;  // why the registry does not read right; then there are no types
};

BlockTypes blockTypes(World const& world);

std::size_t stateCount(std::vector<BlockType> const& types);

// The state's CompoundTag, { name, states, version }, as the game writes it: a compound body (entries + End).
void writeStateTag(std::uintptr_t state, nbt::Buffer& out);

// Its `name`; "" when unreadable.
std::string stateName(std::uintptr_t state);

struct AABB {
    float minX, minY, minZ, maxX, maxY, maxZ;
};

// The boxes the game collides with for the state, where the block is; nullopt when the game's call fails or hangs.
std::optional<std::vector<AABB>> collisionShape(World const& world, BlockType const& type, std::uintptr_t state);

// The box the game draws the state in (getVisualShape), and the one its outline is (getUIShape), which need no world;
// nullopt when the game's call fails or hangs.
std::optional<AABB> visualShape(BlockType const& type, std::uintptr_t state);
std::optional<AABB> uiShape(BlockType const& type, std::uintptr_t state);

// The getOutline most types inherit, for outlineShape
std::uintptr_t inheritedOutline(std::vector<BlockType> const& types);

// The box a player's cursor outlines on the state, where the block is; nullopt when the game's call fails or hangs.
std::optional<AABB> outlineShape(World const& world, BlockType const& type, std::uintptr_t state, std::uintptr_t inherited);

// Whether the build has a camera shape (getCollisionShapeForCamera, from 1.20.30)
bool hasCameraShape();

// The box the camera collides with, or none; nullopt when the game's call fails or hangs.
std::optional<std::optional<AABB>> cameraShape(World const& world, BlockType const& type, std::uintptr_t state);

// What a state is made of. Builds that keep a fact on the block type give every state of the type the same.
struct StateFacts {
    float hardness;  // the destroy speed: how long it takes to dig; -1 where it cannot be dug
    float explosionResistance;
    float friction;
    int   flameOdds;         // how readily fire catches on it
    int   burnOdds;          // how readily fire burns it away
    int   lightEmission;     // 0 - 15
    int   lightDampening;    // 0 - 15: how much light it takes away passing through
    int   data;              // its data value (Block::mData)
    bool  canContainLiquid;  // it can be waterlogged
    bool  operator==(StateFacts const&) const = default;
};

struct BlockFacts {
    int                      id;                   // the block's numeric id: air 0, stone 1, dirt 3
    std::string              descriptionId;        // "tile.stone"; the language file has it + ".name"
    bool                     requiresCorrectTool;  // it drops only when dug by an item whose canDestroySpecial holds
    std::vector<std::string> tags;                 // "minecraft:stone": sorted, with their namespace
    float                    translucency;
    std::vector<StateFacts>  states;  // in the type's state order
};

struct AllBlockFacts {
    std::vector<BlockFacts> types;  // in the order of the types asked about
    std::string             error;  // why they do not read right; then there are none
};

AllBlockFacts blockFacts(std::vector<BlockType> const& types);

}
