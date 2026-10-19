#pragma once
// What each item does to each block state, as the game answers it: the same on every build.

#include "game/Blocks.h"
#include "game/Items.h"

#include <string>
#include <utility>
#include <vector>

namespace bdg::game {

// How long a block takes to break is Player::getDestroyProgress, per tick: at once at hardness 0, else
// speed / hardness / (canDestroy ? 30 : 100), where speed is the held item's getDestroySpeed (times haste, mining
// fatigue, water, not standing) and canDestroy is "the block does not need the right tool, or the held item's
// canDestroySpecial holds". These are those two answers of every item, held as a stack of one without enchantments.
struct DigAnswer {
    std::vector<std::pair<std::string, float>> speeds;   // by item name: Item::getDestroySpeed, where it is not 1
    std::vector<std::string>                   harvest;  // the items whose Item::canDestroySpecial holds
    bool                                       operator==(DigAnswer const&) const = default;
};

struct Dig {
    std::vector<std::vector<DigAnswer>> types;  // per block type in the order asked about, per state in its order
    std::string                         error;  // why the game's answers were not found, or a call faulted; then there are none
};

Dig dig(ItemTypes const& items, std::vector<BlockType> const& types);

}
