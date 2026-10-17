#pragma once
// The game objects the export reads, whichever way a build keeps them (bindings/Steps.cpp). Everything in game/ is
// SEH-safe: a wrong offset or slot gives nothing found, never a crash.

#include <cstdint>
#include <optional>

namespace bdg::game {

struct World {
    std::uintptr_t level;
    std::uintptr_t blockTypeMap;  // the registry's std::map of every block type
    std::uintptr_t overworld;     // the overworld's BlockSource
};

// From Level::tick's `this`; nullopt until the game has them all (the overworld may not exist on the first ticks).
std::optional<World> findWorld(std::uintptr_t tickSelf);

}
