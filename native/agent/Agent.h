#pragma once
// What the agent does in a server, the same on every OS; EntryWindows.cpp and EntryLinux.cpp load it, hook Level::tick
// and guard each attempt. Env (src/agent.ts sets it):
//   BDG_VERSION  the exact server build (1.21.42.01): selects the bindings steps
//   BDG_OUT      where each exporter's file, agent.log and, last, result.txt ("ok ..." or "FAILED: <why>") go

#include "bindings/Bindings.h"

#include <cstdint>
#include <string>

namespace bdg::agent {

// Reads the env and opens the log; false (and nothing more is done) without BDG_OUT and BDG_VERSION.
bool start();

::bdg::bindings::Registry& bindings();

// Writes result.txt, once.
void finish(std::string const& result);

[[nodiscard]] bool done();

// After each of the game's own ticks, on the tick thread: an attempt every few ticks, run by `attempt` under the OS's
// guard; it returns whether the attempt is over.
using GuardedAttempt = bool (*)(std::uintptr_t tickSelf);
void onTick(std::uintptr_t tickSelf, GuardedAttempt attempt);

// One attempt at the export: false until the game has its World.
bool attempt(std::uintptr_t tickSelf);

[[nodiscard]] std::string const& out();

}
