#pragma once
// The enchantments, the same on every build. They are found and read by content (Enchants.cpp): no binding says where
// they are or how an Enchant is laid out.

#include <string>
#include <vector>

namespace bdg::game {

struct Enchant {
    int              id;              // Enchant::Type, the id an item's ench list names it by: protection 0
    std::string      name;            // "protection"
    std::string      descriptionId;   // "enchantment.protect.all"; the language file has it as it is
    int              frequency;       // its weight among the enchantments an enchanting table offers: 30, 10, 3 or 1
    bool             tradeable;       // false for those no villager trades: soul speed, swift sneak, wind burst
    int              primarySlots;    // Enchant::Slot bits of the items it is offered for
    int              secondarySlots;  // ... and those it can be put on at an anvil
    int              minLevel, maxLevel;
    std::vector<int> minCost, maxCost;  // Enchant::getMinCost / getMaxCost at levels 1, 2, ... maxLevel (at least 2)
    std::vector<int> incompatible;      // the other enchantments Enchant::isCompatibleWith answers false for
    bool             treasureOnly;
    bool             discoverable;
};

struct Enchants {
    std::vector<Enchant> enchants;  // by id, from 0
    std::string          error;     // why they do not read right; then there are none
};

Enchants enchants();

}
