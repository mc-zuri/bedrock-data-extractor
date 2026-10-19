#pragma once
// The item registry, the same on every build. It is found and read by content: no binding says where it is or how
// an Item is laid out, but every build has a few items whose values are certain, and those fix where the fields are.

#include "game/Blocks.h"
#include "game/World.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bdg::game {

struct ItemType {
    std::string              name;           // "minecraft:diamond_sword"
    std::uintptr_t           address;        // Item*
    int                      id;             // the runtime id the server sends a client for it
    std::string              descriptionId;  // "item.diamond_sword"; the language file has it + ".name"
    int                      maxStackSize;   // Item::getMaxStackSize (a bucket's by what it holds)
    int                      maxDamage;      // Item::getMaxDamage: 0 where it does not wear out
    std::vector<std::string> tags;           // Item::mTags: "minecraft:is_sword", sorted, with their namespace
};

// An old name whose data values became items of their own (minecraft:log: oak_log, spruce_log, ...), which a recipe
// ingredient may name for any of them.
struct ComplexAlias {
    std::string              name;
    std::vector<std::string> items;  // by data value; an item twice where two values give it
};

struct ItemTypes {
    std::vector<ItemType>                            types;           // by name
    std::vector<std::pair<std::string, std::string>> aliases;         // an old name the game still reads -> the item's name
    std::vector<ComplexAlias>                        complexAliases;  // by name; none before the builds that have them
    std::string                                      error;           // why the registry does not read right; then there are no types
};

ItemTypes itemTypes(World const& world);

// What each item answers (ItemAnswers.cpp); error set when a call is not found, faults or answers wrong.
struct ItemAnswers {
    // Item::getEnchantSlot: Enchant::Slot bits (armor head 1, torso 2, feet 4, legs 8, sword 16, bow 32, ...; -1, every
    // bit, for the books)
    std::map<std::string, int>                      enchantSlots;
    std::map<std::string, std::vector<std::string>> repairItems;  // for each item that wears out, the items Item::isValidRepairItem takes
    // Item::buildDescriptionName of a stack of one (a block item's holding its block's default state): the server's
    // en_US name, the key it builds where its language has none, or "" where it names the item nothing
    std::map<std::string, std::string> names;
    std::string                        error;
};

ItemAnswers itemAnswers(ItemTypes const& items, std::vector<BlockType> const& blocks);

struct Food {
    int   nutrition;
    float saturationModifier;
};

// Each item a player eats, as its legacy food component has it (Foods.cpp); empty when no layout fits the known foods.
std::map<std::string, Food> foods(ItemTypes const& items);

}
