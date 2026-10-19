#include "game/Layout.h"

#include "bindings/Bindings.h"
#include "bindings/Names.h"
#include "core/Log.h"
#include "game/Registry.h"
#include "memory/Mem.h"
#include "memory/Rtti.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <functional>
#include <map>
#include <utility>
#include <vector>

// Each member is the one offset where every block the game is known for reads as it is known to: a BlockLegacy's
// permutations a vector of Blocks (their RTTI says so) with its default state right after it, a Block's serialization
// id its CompoundTag (by the vtable), the description ids, the scalars of the default states (stone's hardness 1.5,
// obsidian's resistance 1200, glowstone's light 15, ...), the drop strategy an object whose class names it a drop
// strategy. Where more than one offset fits, none is taken. The game's operator delete is where the deleting destructors
// of CompoundTag and Item go once they have destroyed (both, one target).

namespace bdg::game {

namespace n = bindings::names;
using memory::readPtr;

namespace {

using Fits = std::function<bool(std::int64_t)>;

// the one offset in [from, to) by `step` that fits; -1 when none or several do
std::int64_t onlyOffset(std::int64_t from, std::int64_t to, std::int64_t step, Fits const& fits, std::string& why, char const* what) {
    std::vector<std::int64_t> found;
    for (std::int64_t off = from; off < to; off += step) {
        if (fits(off)) {
            found.push_back(off);
        }
    }
    if (found.size() == 1) {
        return found[0];
    }
    std::string list;
    for (std::int64_t off : found) {
        list += std::format(" +0x{:X}", off);
    }
    why = std::format("{}: {} offsets fit{}", what, found.size(), list);
    return -1;
}

class Finder {
public:
    explicit Finder(World const& world) {
        forEachBlockType(world.blockTypeMap, [&](std::string const& name, std::uintptr_t type) { mTypes.emplace(name, type); });
    }

    std::uintptr_t type(char const* name) const {
        auto const it = mTypes.find(std::string("minecraft:") + name);
        return it == mTypes.end() ? 0 : it->second;
    }

    // the default state of a type, once the permutations are found
    std::uintptr_t state(char const* name) const { return type(name) ? defaultState(type(name)) : 0; }

private:
    std::map<std::string, std::uintptr_t> mTypes;
};

std::int64_t permutationsAt(Finder const& f, std::uintptr_t blockVtable, std::string& why) {
    auto isVector = [&](std::uintptr_t type, std::int64_t off) {
        std::uintptr_t const begin = readPtr(type, off), end = readPtr(type, off + 8), capacity = readPtr(type, off + 16), byDefault = readPtr(type, off + 24);
        if (!type || !begin || end <= begin || capacity < end || (end - begin) % 8 || (end - begin) / 8 > 4096) {
            return false;
        }
        // a type's permutations may leave states out (null: a torch's 8 for its 6 directions)
        bool hasDefault = false;
        for (std::uintptr_t at = begin; at < end; at += 8) {
            std::uintptr_t const block = readPtr(at);
            if (block && readPtr(block) != blockVtable) {
                return false;
            }
            hasDefault |= block && block == byDefault;
        }
        return hasDefault;
    };
    std::int64_t const found = onlyOffset(
        8, 0x800, 8,
        [&](std::int64_t off) { return isVector(f.type("stone"), off) && isVector(f.type("dirt"), off) && isVector(f.type("glass"), off) && isVector(f.type("torch"), off); }, why,
        "BlockLegacy::mBlockPermutations");
    if (found < 0) {
        // what stone holds that is shaped like a vector of pointers: where it is, how many, the class of the first
        std::uintptr_t const stone = f.type("stone");
        why += std::format(" (stone 0x{:X}, Block's vtable rva 0x{:X}:", stone, blockVtable - memory::imageBase());
        for (std::int64_t off = 8, shown = 0; off < 0x800 && shown < 8; off += 8) {
            std::uintptr_t const begin = readPtr(stone, off), end = readPtr(stone, off + 8), capacity = readPtr(stone, off + 16);
            if (!begin || end <= begin || capacity < end || (end - begin) % 8 || (end - begin) / 8 > 64) {
                continue;
            }
            std::uintptr_t const first = readPtr(begin);
            why += std::format(" +0x{:X}: {} of {} (vtable rva 0x{:X})", off, (end - begin) / 8, memory::classOf(first), readPtr(first) - memory::imageBase());
            ++shown;
        }
        why += ")";
    }
    return found;
}

std::int64_t floatAt(Finder const& f, std::initializer_list<std::pair<char const*, float>> known, std::string& why, char const* what) {
    return onlyOffset(
        8, 0x200, 4,
        [&](std::int64_t off) {
            for (auto const& [name, value] : known) {
                if (!f.state(name) || memory::read<float>(f.state(name), off) != value) {
                    return false;
                }
            }
            return true;
        },
        why, what);
}

std::int64_t byteAt(Finder const& f, std::initializer_list<std::pair<char const*, int>> known, std::string& why, char const* what) {
    return onlyOffset(
        8, 0x200, 1,
        [&](std::int64_t off) {
            for (auto const& [name, value] : known) {
                if (!f.state(name) || memory::read<std::uint8_t>(f.state(name), off) != value) {
                    return false;
                }
            }
            return true;
        },
        why, what);
}

// the targets of the `jmp rel32`s in the first bytes of a deleting destructor (slot 1): one is its tail call, after it
// has destroyed the object, to operator delete
std::vector<std::uintptr_t> jumpsOfDestructor(std::uintptr_t vtable) {
    std::vector<std::uintptr_t> out;
    std::uintptr_t const        fn = readPtr(vtable, 8);
    std::uint8_t                code[96];
    if (!memory::inImage(fn) || !memory::safeRead(reinterpret_cast<void const*>(fn), code, sizeof code)) {
        return out;
    }
    for (std::size_t i = 0; i + 5 <= sizeof code; ++i) {
        if (code[i] == 0xE9) {
            std::int32_t rel;
            std::memcpy(&rel, code + i + 1, 4);
            if (std::uintptr_t const target = fn + i + 5 + static_cast<std::intptr_t>(rel); memory::inImage(target)) {
                out.push_back(target);
            }
        }
    }
    return out;
}

// the one target CompoundTag's and Item's deleting destructors both jump to
std::uintptr_t operatorDelete(std::uintptr_t tagVtable, std::uintptr_t itemVtable) {
    std::vector<std::uintptr_t> common;
    for (std::uintptr_t a : jumpsOfDestructor(tagVtable)) {
        for (std::uintptr_t b : jumpsOfDestructor(itemVtable)) {
            if (a == b && std::ranges::find(common, a) == common.end()) {
                common.push_back(a);
            }
        }
    }
    return common.size() == 1 ? common[0] : 0;
}

}

std::string findLayout(World const& world) {
    auto& r = bindings::current();
    if (!std::as_const(r).variant(n::LayoutByContent, 0)) {
        return "";
    }
    std::uintptr_t const blockVtable = memory::vtableOf("5Block"), tagVtable = memory::vtableOf("11CompoundTag"), itemVtable = memory::vtableOf("4Item");
    if (!blockVtable || !tagVtable || !itemVtable) {
        return std::format("no RTTI of Block (0x{:X}), CompoundTag (0x{:X}) or Item (0x{:X})", blockVtable, tagVtable, itemVtable);
    }
    Finder const f(world);
    std::string  why;
    auto         found = [&](char const* name, std::int64_t off) {
        if (off < 0 || std::as_const(r).offset(name) >= 0) {
            return;
        }
        r.foundOffset(name, off);
        log::info("layout: {} at +0x{:X}", name, off);
    };

    found(n::BlockTypePermutations, permutationsAt(f, blockVtable, why));
    if (!why.empty()) {
        return why;
    }
    found(n::BlockSerializationId, onlyOffset(
                                       8, 0x400, 8,
                                       [&](std::int64_t off) {
                                           return readPtr(f.state("stone"), off) == tagVtable && readPtr(f.state("dirt"), off) == tagVtable &&
                                                  readPtr(f.state("glass"), off) == tagVtable;
                                       },
                                       why, "Block::mSerializationId"));
    found(n::BlockTypeDescriptionId,
          onlyOffset(
              8, 0x800, 8,
              [&](std::int64_t off) { return memory::readString(f.type("obsidian") + off) == "tile.obsidian" && memory::readString(f.type("glass") + off) == "tile.glass"; }, why,
              "BlockLegacy::mDescriptionId"));
    found(n::BlockDestroySpeed, floatAt(f, {{"stone", 1.5f}, {"dirt", 0.5f}, {"obsidian", 35.0f}, {"bedrock", -1.0f}, {"glass", 0.3f}}, why, "Block::mDestroySpeed"));
    found(n::BlockExplosionResistance,
          floatAt(f, {{"stone", 6.0f}, {"dirt", 0.5f}, {"obsidian", 1200.0f}, {"bedrock", 3600000.0f}, {"glass", 0.3f}}, why, "Block::mExplosionResistance"));
    found(n::BlockLightEmission, byteAt(f, {{"glowstone", 15}, {"torch", 14}, {"redstone_torch", 7}, {"stone", 0}, {"glass", 0}}, why, "Block::mLightEmission"));
    found(n::BlockLightDampening, byteAt(f, {{"stone", 15}, {"glowstone", 15}, {"glass", 0}, {"air", 0}, {"oak_leaves", 1}, {"ice", 3}, {"torch", 0}}, why, "Block::mLightBlock"));
    found(n::BlockTypeDropStrategy, onlyOffset(
                                        8, 0x800, 8,
                                        [&](std::int64_t off) {
                                            for (char const* name : {"stone", "dirt", "obsidian"}) {
                                                if (memory::classOf(readPtr(f.type(name), off)).find("DropStrategy") == std::string::npos) {
                                                    return false;
                                                }
                                            }
                                            return true;
                                        },
                                        why, "BlockLegacy::mResourceDropsStrategy"));
    if (!why.empty()) {
        return why;
    }

    if (!r.has(n::OperatorDelete)) {
        std::uintptr_t const found = operatorDelete(tagVtable, itemVtable);
        if (!found) {
            return "operator delete: CompoundTag's and Item's deleting destructors do not jump to one function";
        }
        r.foundFunction(n::OperatorDelete, found);
        log::info("layout: operator delete at rva 0x{:X}", found - memory::imageBase());
    }
    return "";
}

}
