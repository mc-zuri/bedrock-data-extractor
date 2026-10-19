#include "agent/Agent.h"

#include "bindings/Names.h"
#include "core/Files.h"
#include "core/Log.h"
#include "export/Exporter.h"
#include "game/Blocks.h"
#include "game/Items.h"
#include "game/Layout.h"
#include "game/World.h"
#include "memory/Stl.h"

#include <atomic>
#include <cstdlib>
#include <memory>

namespace bdg::agent {

namespace {

// the blocks after the shapes: their drops set blocks in the world (far from where the shapes are asked)
constexpr exporter::Exporter exporters[] = {&exporter::exportPalette, &exporter::exportShapes,  &exporter::exportBlocks, &exporter::exportItems, &exporter::exportEnchantments,
                                            &exporter::exportBiomes,  &exporter::exportEffects, &exporter::exportEnums,  &exporter::exportTints};

std::string       gOut;
std::atomic<bool> gDone{false};

constexpr int MaxAgain = 5;

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
// calls. It runs on the server's own thread: from 1.21 the game reaches the item registry through thread-local storage,
// which another thread does not have. An exporter whose failure a later tick may not have is tried again on the next
// attempts, from where it stopped.
bool attempt(std::uintptr_t tickSelf) {
    static std::unique_ptr<exporter::Context> context;
    static std::size_t                        next  = 0;
    static int                                again = 0;
    static std::string                        wrote;
    if (!context) {
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
        auto items = game::itemTypes(*world);
        if (!items.error.empty()) {
            finish("FAILED: items: " + items.error);
            return true;
        }
        context = std::make_unique<exporter::Context>(exporter::Context{*world, std::move(blockTypes.types), std::move(items), gOut});
    }
    for (; next < std::size(exporters); ++next) {
        auto result = exporters[next](*context);
        if (!result.ok && result.again && ++again <= MaxAgain) {
            log::info("{}: trying again on a later tick ({} of {})", result.message, again, MaxAgain);
            return false;
        }
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
