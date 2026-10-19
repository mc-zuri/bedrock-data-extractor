#include "game/Items.h"

#include "core/Log.h"
#include "memory/Mem.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

// Item::mFoodComponentLegacy, a pointer to the item's FoodItemComponentLegacy (null on an item that is not eaten), and
// in it the nutrition (an int) and the saturation modifier (a float): the offsets at which the known foods read their
// known values and the known non-foods have no component. The known values are those the server sends from 1.18.11
// (item_component), the same on every build. From about 1.20.50 some foods (an apple) are data-driven and keep theirs
// in their item components instead: no layout fits, and the packets say them.

namespace bdg::game {

using memory::readPtr;

namespace {

struct KnownFood {
    char const* name;
    int         nutrition;
    float       saturationModifier;
};

KnownFood const knownFoods[] = {
    {"minecraft:apple", 4, 0.3f},  {"minecraft:bread", 5, 0.6f},         {"minecraft:cooked_beef", 8, 0.8f},
    {"minecraft:cookie", 2, 0.1f}, {"minecraft:golden_carrot", 6, 1.2f}, {"minecraft:rotten_flesh", 4, 0.1f},
};
char const* const knownNonFoods[] = {"minecraft:stick", "minecraft:diamond_sword", "minecraft:stone", "minecraft:bow"};

bool isObject(std::uintptr_t p) { return p >= 0x10000 && !(p & 7) && memory::inImage(readPtr(p)); }

struct FoodLayout {
    std::int64_t component = -1, nutrition = -1, saturationModifier = -1;
};

}

std::map<std::string, Food> foods(ItemTypes const& items) {
    std::map<std::string, std::uintptr_t> byName;
    for (ItemType const& t : items.types) {
        byName.emplace(t.name, t.address);
    }
    auto item = [&](char const* name) -> std::uintptr_t {
        auto it = byName.find(name);
        return it == byName.end() ? 0 : it->second;
    };
    for (KnownFood const& k : knownFoods) {
        if (!item(k.name)) {
            return {};
        }
    }
    for (char const* name : knownNonFoods) {
        if (!item(name)) {
            return {};
        }
    }

    std::vector<FoodLayout> fits;
    for (std::int64_t component = 8; component <= 0x400; component += 8) {
        bool const nonFoods = std::ranges::all_of(knownNonFoods, [&](char const* name) { return readPtr(item(name), component) == 0; });
        if (!nonFoods || !std::ranges::all_of(knownFoods, [&](KnownFood const& k) { return isObject(readPtr(item(k.name), component)); })) {
            continue;
        }
        auto fieldAt = [&](auto read) {
            std::int64_t found = -1;
            for (std::int64_t off = 8; off <= 0x80; off += 4) {
                if (std::ranges::all_of(knownFoods, [&](KnownFood const& k) { return read(readPtr(item(k.name), component), off, k); })) {
                    if (found >= 0) {
                        return std::int64_t{-2};
                    }
                    found = off;
                }
            }
            return found;
        };
        std::int64_t const nutrition = fieldAt([](std::uintptr_t c, std::int64_t off, KnownFood const& k) { return memory::read<std::int32_t>(c, off) == k.nutrition; });
        std::int64_t const modifier  = fieldAt([](std::uintptr_t c, std::int64_t off, KnownFood const& k) { return memory::read<float>(c, off) == k.saturationModifier; });
        if (nutrition >= 0 && modifier >= 0) {
            fits.push_back({component, nutrition, modifier});
        }
    }
    if (fits.size() != 1) {
        log::info("items: {} offsets fit a food component", fits.size());
        return {};
    }
    FoodLayout const            layout = fits[0];
    std::map<std::string, Food> out;
    for (ItemType const& t : items.types) {
        std::uintptr_t const component = readPtr(t.address, layout.component);
        if (component == 0) {
            continue;
        }
        int const   nutrition = memory::read<std::int32_t>(component, layout.nutrition);
        float const modifier  = memory::read<float>(component, layout.saturationModifier);
        if (!isObject(component) || nutrition < 0 || nutrition > 20 || !std::isfinite(modifier) || modifier < 0 || modifier > 2) {
            log::info("items: {}'s food component reads nutrition {}, saturation modifier {}", t.name, nutrition, modifier);
            return {};
        }
        out.emplace(t.name, Food{nutrition, modifier});
    }
    log::info("items: food component at +{} (nutrition +{}, saturation modifier +{}), {} foods", layout.component, layout.nutrition, layout.saturationModifier, out.size());
    return out;
}

}
