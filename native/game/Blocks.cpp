#include "game/Blocks.h"

#include "core/Log.h"
#include "game/Registry.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace bdg::game {

namespace {

bool sameIgnoringCase(std::string const& a, std::string const& b) {
    return std::ranges::equal(a, b, [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
}

}

BlockTypes blockTypes(World const& world) {
    BlockTypes list;
    forEachBlockType(world.blockTypeMap, [&](std::string const& key, std::uintptr_t address) {
        if (!list.error.empty()) {
            return;
        }
        auto states = permutations(address);
        // the map's key is the name lowercased before 1.18.30: the states' own name is the game's
        std::string       name = states.empty() ? "" : stateName(states.front());
        std::size_t const def  = std::ranges::find(states, defaultState(address)) - states.begin();
        if (states.empty()) {
            list.error = std::format("{} has no states", key);
        } else if (!sameIgnoringCase(name, key)) {
            list.error = std::format("{} has states named '{}'", key, name);
        } else if (def == states.size()) {
            list.error = std::format("the default state of {} is none of its states", key);
        } else {
            list.types.push_back({name, address, std::move(states), def});
        }
    });
    if (!list.error.empty()) {
        list.types.clear();
    }
    log::info("{} block types, {} states", list.types.size(), stateCount(list.types));
    return list;
}

std::size_t stateCount(std::vector<BlockType> const& types) {
    std::size_t count = 0;
    for (auto const& type : types) {
        count += type.states.size();
    }
    return count;
}

}
