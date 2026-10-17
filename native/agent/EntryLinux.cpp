// Linux: preloaded into bedrock_server (LD_PRELOAD). Its constructor resolves the bindings, by the server's own symbols
// where it has them, and hooks Level::tick; the attempts start once node writes BDG_OUT/go, after "Server started".
#include "agent/Agent.h"
#include "bindings/Names.h"
#include "core/Files.h"
#include "core/Log.h"
#include "memory/Elf.h"
#include "memory/Guard.h"
#include "memory/Hook.h"
#include "memory/Mem.h"

#include <unistd.h>

#include <climits>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>

namespace {

namespace n = bdg::bindings::names;

using TickFn = void (*)(void*);
TickFn gTick = nullptr;

// The server's symbols for the name bindings: addresses from its symbols file, code and vtables from its live image.
class LiveSymbols : public bdg::bindings::Registry::Symbols {
public:
    explicit LiveSymbols(std::string const& file) { mOpen = mFile.open(file); }
    [[nodiscard]] bool open() const { return mOpen; }

    std::map<std::string, std::uint64_t> addresses(std::vector<std::string> const& mangled) const override {
        return mFile.symbols(std::set<std::string>(mangled.begin(), mangled.end()));
    }
    bool bytes(std::uint64_t rva, void* out, std::size_t n) const override { return bdg::memory::safeRead(reinterpret_cast<void const*>(bdg::memory::imageBase() + rva), out, n); }
    std::uint64_t word(std::uint64_t rva) const override {
        std::uintptr_t const w = bdg::memory::readPtr(bdg::memory::imageBase() + rva);
        return bdg::memory::inImage(w) ? w - bdg::memory::imageBase() : w;
    }

private:
    bdg::memory::ElfFile mFile;
    bool                 mOpen = false;
};

std::string exePath() {
    char          path[PATH_MAX];
    ssize_t const n = readlink("/proc/self/exe", path, sizeof path - 1);
    return n > 0 ? std::string(path, static_cast<std::size_t>(n)) : "";
}

bool guardedAttempt(std::uintptr_t tickSelf) {
    bdg::memory::armFaultHandler();
    bool over = false;
    if (!bdg::memory::guard([&] { over = bdg::agent::attempt(tickSelf); })) {
        auto const f = bdg::memory::lastFault();
        bdg::agent::finish(std::format("FAILED: the export faulted: signal {} at {}+0x{:X}", f.code, f.inImage ? "bedrock_server" : "?", f.rva));
        return true;
    }
    return over;
}

// node's BDG_OUT/go, looked for every few ticks: until it is there, no attempt is made (nor counted)
bool ready() {
    static bool go    = false;
    static int  ticks = 0;
    if (!go && ticks++ % 10 == 0) {
        go = bdg::files::exists(bdg::files::join(bdg::agent::out(), "go"));
    }
    return go;
}

void hookedTick(void* self) {
    gTick(self);
    if (ready()) {
        bdg::agent::onTick(reinterpret_cast<std::uintptr_t>(self), &guardedAttempt);
    }
}

// Only in the server: a process it starts (or one the agent was preloaded into by mistake) is left alone, and the
// server's children are not given the agent.
__attribute__((constructor)) void load() {
    std::string const exe = exePath();
    if (std::string_view(exe).substr(exe.find_last_of('/') + 1) != "bedrock_server") {
        return;
    }
    unsetenv("LD_PRELOAD");
    if (!bdg::agent::start()) {
        return;
    }
    auto&             r       = bdg::agent::bindings();
    std::string const symbols = bdg::memory::symbolsFileOf(exe);
    LiveSymbols const live(symbols);
    if (!live.open()) {
        bdg::agent::finish("FAILED: cannot read the server's symbols in " + symbols);
        return;
    }
    bdg::log::info("symbols: {}, image at 0x{:X}", symbols, bdg::memory::imageBase());
    auto failed = r.resolveNames(live);
    for (auto const& f : r.resolve(bdg::memory::Module::game())) {
        failed.push_back(f);
    }
    for (auto const& [name, value] : r.describe()) {
        bdg::log::info("  {} = {}", name, value);
    }
    if (!r.has(n::LevelTick)) {
        bdg::agent::finish("FAILED: no Level::tick: " + symbols + " has no symbol of it and the bindings no pattern");
        return;
    }
    if (!failed.empty()) {
        std::string list;
        for (auto const& f : failed) {
            list += (list.empty() ? "" : ", ") + f;
        }
        bdg::agent::finish("FAILED: unresolved bindings: " + list);
        return;
    }
    void* const tick = reinterpret_cast<void*>(r.address(n::LevelTick));
    if (!tick || !bdg::memory::hook(tick, reinterpret_cast<void*>(&hookedTick), reinterpret_cast<void**>(&gTick))) {
        bdg::agent::finish("FAILED: could not hook Level::tick");
        return;
    }
    bdg::log::info("hooked Level::tick at 0x{:X}", r.rvaOf(n::LevelTick));
}

}
