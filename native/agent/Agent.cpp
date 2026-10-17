#include "agent/Agent.h"

#include "bindings/Names.h"
#include "core/Files.h"
#include "core/Log.h"
#include "export/Exporter.h"
#include "game/Blocks.h"
#include "game/Layout.h"
#include "game/World.h"
#include "memory/Stl.h"

#include <atomic>
#include <cstdlib>

namespace bdg::agent {

namespace {

constexpr exporter::Exporter exporters[] = {&exporter::exportPalette, &exporter::exportShapes};

std::string       gOut;
std::atomic<bool> gDone{false};

std::string env(char const* name) {
    char const* v = std::getenv(name);
    return v ? v : "";
}

}

bool start() {
    gOut                = env("BDG_OUT");
    std::string version = env("BDG_VERSION");
    if (gOut.empty() || version.empty()) {
        return false;
    }
    log::open(files::join(gOut, "agent.log"));
    log::info("bdg agent: {}", version);
    auto const& r     = ::bdg::bindings::load(Version::parse(version));
    memory::stlKind() = static_cast<memory::Stl>(r.variant(::bdg::bindings::names::StlKind, static_cast<int>(memory::stlKind())));
    return true;
}

::bdg::bindings::Registry& bindings() { return ::bdg::bindings::current(); }

std::string const& out() { return gOut; }

void finish(std::string const& result) {
    if (gDone.exchange(true)) {
        return;
    }
    log::info("result: {}", result);
    files::writeWhole(files::join(gOut, "result.txt"), result.data(), result.size());
}

bool done() { return gDone.load(); }

// On the tick thread, right after the game's own tick: the game waits for it, so nothing changes under the walk or the
// calls.
bool attempt(std::uintptr_t tickSelf) {
    auto world = game::findWorld(tickSelf);
    if (!world) {
        return false;
    }
    if (std::string const why = game::findLayout(*world); !why.empty()) {
        finish("FAILED: layout: " + why);
        return true;
    }
    auto blockTypes = game::blockTypes(*world);
    if (!blockTypes.error.empty()) {
        finish("FAILED: " + blockTypes.error);
        return true;
    }
    exporter::Context const context{*world, std::move(blockTypes.types), gOut};
    std::string             wrote;
    for (auto const exporter : exporters) {
        auto result = exporter(context);
        if (!result.ok) {
            finish("FAILED: " + result.message);
            return true;
        }
        wrote += (wrote.empty() ? "" : ", ") + result.message;
    }
    finish("ok " + wrote);
    return true;
}

constexpr int TryEveryTicks = 10;  // ~0.5 s at 20 tps
constexpr int MaxAttempts   = 60;  // ~30 s of ticks before giving up on findWorld

void onTick(std::uintptr_t tickSelf, GuardedAttempt guarded) {
    if (gDone.load()) {
        return;
    }
    static int ticks = 0, attempts = 0;
    if (ticks++ % TryEveryTicks) {
        return;
    }
    if (++attempts > MaxAttempts) {
        finish("FAILED: no Level with a block type registry and an overworld found on the tick");
        return;
    }
    guarded(tickSelf);
}

}
