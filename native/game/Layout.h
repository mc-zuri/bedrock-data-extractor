#pragma once
// Linux builds without symbols (the bindings' LayoutByContent variant): the members the bindings cannot name, found in
// the running server by content and checked on what the game is known to answer, then set into the bindings for the
// rest of the agent to read as any other.

#include "game/World.h"

#include <string>

namespace bdg::game {

// "" once every member is found (or the build binds them all); else why one is not
std::string findLayout(World const& world);

}
