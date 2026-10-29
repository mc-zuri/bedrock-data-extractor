#include "game/Drops.h"

#include "bindings/Bindings.h"
#include "bindings/Names.h"
#include "core/Log.h"
#include "game/MapColor.h"
#include "game/Registry.h"
#include "game/Virtuals.h"
#include "memory/Flow.h"
#include "memory/Guard.h"
#include "memory/Hook.h"
#include "memory/Mem.h"
#include "memory/Rtti.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_set>

// A block's drops are code (each block type's drop logic, of chances and counts), so they are drawn, not read: the
// game's own drop code is asked what one mining drops, again and again, from a Random the agent seeds (each type by its
// name, every state of it the same `samples` times), so the same code gives the same answers here and in every build
// that has it. The code is asked one of two ways:
// - from 1.20.71 every block type has a drop strategy (BlockLegacy::mResourceDropsStrategy), and
//   BlockLegacy::getResourceDrops is its IResourceDropsStrategy::getResourceDrops (vtable slot 1): ResourceDrops
//   (Block const&, Randomize&, ResourceDropsContext const&), the stacks one mining drops. Data-driven types (1.26.51's
//   wool and concrete slabs) have none: their minecraft:loot table drops for them, which the agent leaves to the server's
//   behavior packs. The stacks the game returns are destroyed as the game would (each ItemStack's virtual destructor,
//   then the vector's buffer, on the heap the agent shares with it).
// - before, block types spawn their drops themselves (BlockLegacy::spawnResources, which many override): the agent calls
//   Block::spawnResources (bindings: its overload and SpawnResourcesKind) and takes each item it spawns from
//   Spawner::spawnItem, hooked to spawn nothing while it does. Up to 1.18.0 a skull spawns its item when it is removed
//   (SkullBlock::onRemove): there a draw that spawns nothing asks BlockLegacy::onRemove as well.
// Either way a type that keeps a block actor (a chest, a banner, a skull) drops what its actor says, read from the
// world where the drop code is told the block is: its default state is set there first (BlockSource::setBlock), in a
// chunk the server keeps loaded (a ticking area); where onRemove is asked, which reads the block too, the state itself.
// A stack's data value is read twice: its own, and as ItemStackBase::getAuxValue reads it (the data of the block it
// carries), which is what the network sent before 1.16.220.
// The Random is a copy of one the Level owns (found by its nextFloat), its Mersenne Twister written for each seed as
// Core::Random::_setSeed writes it; before 1.17.10 the drop code draws from the Level's own Random, which the agent
// seeds in place. The Level's own is seeded the same with the copy: 1.20.61's BlockLegacy::getResourceDrops draws a
// count range (the dead bush's) from it (ResourceDropsContext::getRandom, the Level's getRandom).

namespace bdg::game {

namespace n = bindings::names;
using memory::inImage;
using memory::read;
using memory::readPtr;

namespace {

// ResourceDropsContext as ResourceDropsContext::fromPlayerMining makes it: the cause (0, a player mining), a chance
// factor, the stack mined with, where, the dimension, the BlockSource
struct Context {
    int            cause  = 0;
    float          chance = 1.0f;
    std::uintptr_t tool   = 0;
    int            x = 0, y = 64, z = 0;
    int            dimension = 0;
    std::uintptr_t region    = 0;
};
static_assert(sizeof(Context) == 0x28);

// ResourceDrops: a std::vector<ItemStack>, then an int
struct Stacks {
    std::uintptr_t begin = 0, end = 0, capacity = 0;
    std::uint64_t  extra = 0;
};

struct BlockPos {
    int x = 0, y = 0, z = 0;
};

// Block::getMapColor of a state where it is (false when it faults), as findMapColor finds it
using Ask = std::function<bool(std::uintptr_t state, BlockPos const* pos, Color4* out)>;

// The middle of the chunk the ticking area node adds keeps loaded (src/agent.ts: chunk 64, 64): far from the world
// origin, where the collision shapes are asked with no chunk loaded around them.
constexpr BlockPos DrawnAt{1032, 64, 1032};

// one stack one mining drops
struct Stack {
    std::uintptr_t item;  // Item*
    int            count, aux, data;
};

std::int64_t gBlockDataAt = -1;  // Block::mData (blockDataOffset)

// an ItemStack's item (its WeakPtr<Item> at +8 points at the Item*), count, data value, and the data value
// ItemStackBase::getAuxValue reads: the Block's it carries (at +0x18), unless its own is 0x7FFF (any)
Stack stackAt(std::uintptr_t stack) {
    std::uintptr_t const block = readPtr(stack + 0x18);
    std::int16_t const   aux   = read<std::int16_t>(stack, 0x20);
    return {readPtr(readPtr(stack + 8)), read<std::uint8_t>(stack, 0x22), aux, block && aux != 0x7FFF ? read<std::uint16_t>(block, gBlockDataAt) : aux};
}

// the vtable of one of the game's own stacks, for the stacks of one the agent asks Item virtuals with (game/Virtuals.h)
void keepStackVtable(std::uintptr_t stack) {
    if (std::uintptr_t const vtable = readPtr(stack); !gStackVtable && inImage(vtable)) {
        gStackVtable = vtable;
    }
}

// the exception (the signal, on Linux) a call below faulted with, and where
std::string lastFault() {
    auto const f = memory::lastFault();
    return std::format("exception 0x{:X} at {} 0x{:X}", f.code, f.inImage ? "rva" : "address", f.rva);
}

// BlockLegacy::mBlockEntityType: the one byte offset where the block types that keep a block actor read its type
// (BlockActorType: furnace 1, chest 2, sign 4, hopper 15) and plain ones (air, stone, dirt) 0; -1 when none or several
std::int64_t blockActorTypeAt(std::vector<BlockType> const& types) {
    auto address = [&](char const* name) {
        auto const it = std::ranges::find(types, std::string(name), &BlockType::name);
        return it == types.end() ? std::uintptr_t{0} : it->address;
    };
    std::pair<std::uintptr_t, int> const known[] = {{address("minecraft:furnace"), 1}, {address("minecraft:chest"), 2}, {address("minecraft:standing_sign"), 4},
                                                    {address("minecraft:hopper"), 15}, {address("minecraft:air"), 0},   {address("minecraft:stone"), 0},
                                                    {address("minecraft:dirt"), 0}};
    std::vector<std::int64_t>            found;
    for (std::int64_t at = 8; at < 0x400; ++at) {
        if (std::ranges::all_of(known, [&](auto const& k) { return k.first && read<std::uint8_t>(k.first, at) == k.second; })) {
            found.push_back(at);
        }
    }
    return found.size() == 1 ? found[0] : -1;
}

// BlockSource::setBlock(BlockPos const&, Block const&, int, ActorBlockSyncMessage const*, Actor*), a virtual from 1.20:
// the one slot of the BlockSource's first 64 that only forwards to the setBlock that also takes a
// std::shared_ptr<BlockActor>, an empty one (16 bytes zeroed on the stack, their address its fifth argument). -1 when
// none or several.
int setBlockSlot(std::uintptr_t region) {
    auto forwards = [](std::uint8_t const* code, std::size_t size) {
        for (std::size_t i = 0; i + 6 <= size; ++i) {
            int zeroed = -1;  // movdqu (MSVC) or movaps / movups (clang) [rsp + n], xmm
            if (code[i] == 0xF3 && code[i + 1] == 0x0F && code[i + 2] == 0x7F && (code[i + 3] & 0xC7) == 0x44 && code[i + 4] == 0x24) {
                zeroed = code[i + 5];
            }
            if (code[i] == 0x0F && (code[i + 1] == 0x29 || code[i + 1] == 0x11) && (code[i + 2] & 0xC7) == 0x44 && code[i + 3] == 0x24) {
                zeroed = code[i + 4];
            }
            if (zeroed < 0) {
                continue;
            }
            // lea rax, [rsp + n]; mov [rsp + 20h], rax; then a call
            std::uint8_t const passed[] = {0x48, 0x8D, 0x44, 0x24, static_cast<std::uint8_t>(zeroed), 0x48, 0x89, 0x44, 0x24, 0x20};
            for (std::size_t j = 0; j + sizeof passed < size; ++j) {
                if (!std::memcmp(code + j, passed, sizeof passed) && std::find(code + j + sizeof passed, code + size, 0xE8) != code + size) {
                    return true;
                }
            }
        }
        return false;
    };
    std::vector<int> found;
    for (int slot = 0; slot < 64; ++slot) {
        std::uint8_t         code[64];
        std::uintptr_t const fn = memory::virtualAt(region, slot);
        if (fn && memory::safeRead(reinterpret_cast<void const*>(fn), code, sizeof code) && forwards(code, sizeof code)) {
            found.push_back(slot);
        }
    }
    return found.size() == 1 ? found[0] : -1;
}

// Block::mLegacy: the one offset where the default states of the blocks read their own BlockLegacy; -1 when none or several
std::int64_t legacyAt(std::vector<BlockType> const& types) {
    std::vector<std::int64_t> found;
    for (std::int64_t off = 8; off < 0x200; off += 8) {
        bool const fits = std::ranges::all_of(types, [&](BlockType const& t) { return readPtr(t.states[t.defaultState], off) == t.address; });
        if (fits) {
            found.push_back(off);
        }
    }
    return found.size() == 1 ? found[0] : -1;
}

// The member `fn` returns where it only returns 16 bytes of `this` (BlockLegacy::getMapColor's base: the colour the type
// keeps), loaded from [rcx / rdi + disp] with movups / movsd / movaps, within its first 24 bytes and without a call; -1
// when it does something else
std::int64_t storedColorAt(std::uintptr_t fn) {
    std::uint8_t b[24];
    if (!fn || !memory::safeRead(reinterpret_cast<void const*>(fn), b, sizeof b)) {
        return -1;
    }
    auto const   ret = std::find(b, b + sizeof b, std::uint8_t{0xC3});
    std::int64_t at  = -1;
    for (auto p = b; p + 3 < ret; ++p) {
        if (*p == 0xE8 || *p == 0xE9) {
            return -1;
        }
        std::uint8_t const* op = p[0] == 0xF2 || p[0] == 0x66 ? p + 1 : p;
        if (at < 0 && op[0] == 0x0F && (op[1] == 0x10 || op[1] == 0x28)) {
            std::uint8_t const base = op[2] & 0xC7;
            if (base == 0x81 || base == 0x87) {
                std::int32_t disp;
                std::memcpy(&disp, op + 3, 4);
                at = disp;
            } else if (base == 0x41 || base == 0x47) {
                at = static_cast<std::int8_t>(op[3]);
            }
        }
    }
    return ret != b + sizeof b ? at : -1;
}

// The slots BlockLegacy::getMapColor may be at: of the slots whose inherited function (the one most block types have
// there) returns a colour the type keeps, those where that colour is the known one of the blocks that inherit it
// (MaterialColor's: the black of obsidian, the nether's netherrack, gold, clay, glowstone's sand), read, not called; the
// most overridden first, and those no type overrides (older builds' getMapColor(), its base folded into the same
// function) left out
std::vector<int> mapColorSlots(std::vector<BlockType> const& types) {
    std::pair<char const*, int> const known[] = {
        {"minecraft:obsidian", 0x191919}, {"minecraft:netherrack", 0x700200}, {"minecraft:gold_block", 0xFAEE4D}, {"minecraft:clay", 0xA4A8B8}, {"minecraft:glowstone", 0xF7E9A3}};
    std::vector<int> found;
    for (int slot = 1; slot < 400; ++slot) {
        std::map<std::uintptr_t, int> count;
        for (BlockType const& type : types) {
            ++count[memory::virtualAt(type.address, slot)];
        }
        auto const most = std::ranges::max_element(count, {}, [](auto const& e) { return e.second; });
        if (most == count.end() || !memory::inImage(most->first)) {
            break;
        }
        std::int64_t const at = storedColorAt(most->first);
        if (at < 0) {
            continue;
        }
        int  inheriting = 0;
        bool fits       = true;
        for (auto const& [name, rgb] : known) {
            auto const type = std::ranges::find(types, std::string(name), &BlockType::name);
            if (type == types.end() || memory::virtualAt(type->address, slot) != most->first) {
                continue;
            }
            ++inheriting;
            float const expected[] = {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f};
            for (int i = 0; i < 4; ++i) {
                fits = fits && std::abs(memory::read<float>(type->address, at + 4 * i) - expected[i]) < 1e-6f;
            }
        }
        if (fits && inheriting >= 3) {
            std::size_t const overrides = count.size() - 1;
            found.push_back(slot);
            log::info("map colours: slot {} returns the known colours, {} functions override it", slot, overrides);
        }
    }
    auto overrides = [&](int slot) {
        std::set<std::uintptr_t> fns;
        for (BlockType const& type : types) {
            fns.insert(memory::virtualAt(type.address, slot));
        }
        return fns.size() - 1;
    };
    std::erase_if(found, [&](int slot) { return overrides(slot) == 0; });
    std::ranges::sort(found, std::greater{}, overrides);
    return found;
}

// The functions of the image that call one of `slots` of the vtable of the object their `this` points to at `through` (a
// Block's mLegacy) and write nothing through `this`, as a const member function does not: the function around each place
// that calls or loads [reg + slot * 8] (`call / jmp [reg + slot * 8]`, or the load of a call through Control Flow
// Guard's dispatch or a tail jump), as its unwind data has it, read by its data flow (memory/Flow.h)
std::vector<std::uintptr_t> callersOf(std::vector<int> const& slots, std::int64_t through) {
    std::vector<std::uintptr_t> out;
    auto const [from, to] = memory::imageCode();
    std::vector<std::uint8_t> code(1 << 20);
    auto                      displacement = [](std::uint8_t const* at, std::uint8_t modrm) -> std::int64_t {
        std::uint8_t const rm = modrm & 7, mod = modrm >> 6;
        if (rm == 4 || rm == 5 || (mod != 1 && mod != 2)) {
            return -1;
        }
        std::int32_t disp = 0;
        if (mod == 2) {
            std::memcpy(&disp, at, 4);
        } else {
            disp = static_cast<std::int8_t>(at[0]);
        }
        return disp;
    };
    for (std::uintptr_t at = from; at < to; at += code.size() - 8) {
        std::size_t const n = static_cast<std::size_t>(std::min<std::uintptr_t>(code.size(), to - at));
        if (!memory::safeRead(reinterpret_cast<void const*>(at), code.data(), n)) {
            continue;
        }
        for (std::size_t i = 0; i + 7 <= n; ++i) {
            std::int64_t disp = -1;
            if (code[i] == 0xFF && (((code[i + 1] >> 3) & 7) == 2 || ((code[i + 1] >> 3) & 7) == 4)) {
                disp = displacement(&code[i + 2], code[i + 1]);
            } else if ((code[i] & 0xF0) == 0x40 && (code[i] & 0x08) && code[i + 1] == 0x8B) {
                disp = displacement(&code[i + 3], code[i + 2]);
            }
            if (disp <= 0 || disp % 8 || std::ranges::find(slots, static_cast<int>(disp / 8)) == slots.end()) {
                continue;
            }
            auto const [begin, end] = memory::functionAround(at + i);
            if (begin && end - begin <= 0x4000 && std::ranges::find(out, begin) == out.end() && memory::callsVirtualOfMember(begin, end, through, slots) &&
                !memory::writesThroughThis(begin, end)) {
                out.push_back(begin);
            }
        }
    }
    return out;
}

// the calls, each false when it faults (memory::guard); a setBlock answers whether the block there changed, and
// updates nothing: nothing reacts to the block before its drops are drawn
bool callSetBlockXyz(std::uintptr_t fn, std::uintptr_t region, BlockPos const* pos, std::uintptr_t state) {
    bool changed = false;
    return memory::guard(
               [&] { changed = reinterpret_cast<bool (*)(std::uintptr_t, int, int, int, std::uintptr_t, int, void*)>(fn)(region, pos->x, pos->y, pos->z, state, 0, nullptr); }) &&
           changed;
}

bool callSetBlockAt(std::uintptr_t fn, std::uintptr_t region, BlockPos const* pos, std::uintptr_t state, void const* changer) {
    bool changed = false;
    return memory::guard([&] {
               changed = reinterpret_cast<bool (*)(std::uintptr_t, BlockPos const*, std::uintptr_t, int, void*, void const*)>(fn)(region, pos, state, 0, nullptr, changer);
           }) &&
           changed;
}

// ResourceDrops IResourceDropsStrategy::getResourceDrops(Block const&, Randomize&, ResourceDropsContext const&) const,
// the interface's one virtual after its destructor (which takes one slot with MSVC, two in the Itanium ABI), returned
// through a hidden pointer: after `this` with MSVC, before it in the Itanium ABI
bool callStrategy(std::uintptr_t strategy, Stacks* out, std::uintptr_t state, void* randomize, Context const* context) {
    std::uintptr_t const fn = memory::virtualAt(strategy, memory::DestructorSlots);
    if (!fn) {
        return false;
    }
    return memory::guard([&] {
#ifdef _WIN32
        reinterpret_cast<void* (*)(std::uintptr_t, Stacks*, std::uintptr_t, void*, Context const*)>(fn)(strategy, out, state, randomize, context);
#else
        reinterpret_cast<void* (*)(Stacks*, std::uintptr_t, std::uintptr_t, void*, Context const*)>(fn)(out, strategy, state, randomize, context);
#endif
    });
}

// an ItemStack's virtual destructor, without freeing it (it is in the vector's buffer): slot 0 is MSVC's deleting
// destructor, told (0) not to free; the Itanium ABI's complete-object destructor, which never does
bool destroyStack(std::uintptr_t stack) {
    std::uintptr_t const fn = memory::virtualAt(stack, 0);
    if (!fn) {
        return false;
    }
    return memory::guard([&] { reinterpret_cast<void* (*)(std::uintptr_t, int)>(fn)(stack, 0); });
}

// Block::spawnResources(BlockSource&, BlockPos const&, ...) as the build declares it: a player mining (explosion radius
// 1: always drops), no bonus loot, with an empty hand
bool callSpawnResources(std::uintptr_t fn, int kind, std::uintptr_t state, std::uintptr_t region, BlockPos const* pos, void* randomize, void const* hand, Context const* context) {
    using P    = std::uintptr_t;
    using V    = void*;
    using H    = void const*;
    bool known = true;
    return memory::guard([&] {
               switch (kind) {
               case n::SpawnBlockLevelRandom:
                   reinterpret_cast<void (*)(P, P, BlockPos const*, P, V, float, int, bool)>(fn)(state, region, pos, state, nullptr, 1.0f, 0, true);
                   break;
               case n::SpawnBlockLevelRandomTool:
                   reinterpret_cast<void (*)(P, P, BlockPos const*, P, V, float, int, bool, H)>(fn)(state, region, pos, state, nullptr, 1.0f, 0, true, hand);
                   break;
               case n::SpawnBlockRandomizeLootTool:
                   reinterpret_cast<void (*)(P, P, BlockPos const*, P, V, V, float, int, H)>(fn)(state, region, pos, state, randomize, nullptr, 1.0f, 0, hand);
                   break;
               case n::SpawnBlockRandomizeTool:
                   reinterpret_cast<void (*)(P, P, BlockPos const*, P, V, V, float, H)>(fn)(state, region, pos, state, randomize, nullptr, 1.0f, hand);
                   break;
               case n::SpawnRandomizeVectorTool:
                   reinterpret_cast<void (*)(P, P, BlockPos const*, V, V, float, H)>(fn)(state, region, pos, randomize, nullptr, 1.0f, hand);
                   break;
               case n::SpawnRandomizeTool:
                   reinterpret_cast<void (*)(P, P, BlockPos const*, V, float, H)>(fn)(state, region, pos, randomize, 1.0f, hand);
                   break;
               case n::SpawnRandomizeContext:
                   reinterpret_cast<void (*)(P, P, BlockPos const*, V, Context const*)>(fn)(state, region, pos, randomize, context);
                   break;
               default:
                   known = false;
               }
           }) &&
           known;
}

bool callOnRemove(std::uintptr_t fn, std::uintptr_t type, std::uintptr_t region, BlockPos const* pos) {
    return memory::guard([&] { reinterpret_cast<void (*)(std::uintptr_t, std::uintptr_t, BlockPos const*)>(fn)(type, region, pos); });
}

// Spawner::spawnItem, hooked: while the agent draws (gSpawned set) it keeps what the game would spawn and spawns nothing
using SpawnItem                = void* (*)(void*, void*, std::uintptr_t, void*, void*, int);
SpawnItem           gSpawnItem = nullptr;
std::vector<Stack>* gSpawned   = nullptr;

void* hookedSpawnItem(void* spawner, void* region, std::uintptr_t stack, void* actor, void* pos, int throwTime) {
    if (!gSpawned) {
        return gSpawnItem(spawner, region, stack, actor, pos, throwTime);
    }
    keepStackVtable(stack);
    gSpawned->push_back(stackAt(stack));
    return nullptr;
}

// Random: its IRandom vtable, then its Core::Random (0x20 in up to 1.21.93, 0x18 in later MSVC builds, 8 from 1.26), a
// Mersenne Twister after its own vtable: the seed, its 624 words from +4, the index of the next word at +0x9C4 (624:
// twist first). Core::Random's own fields after those (whether a gaussian is cached, how many words are initialized)
// move between builds; the agent leaves them as the Random it copies has them.
constexpr std::int64_t TwisterWordsAt = 4, TwisterIndexAt = 0x9C4;
constexpr int          TwisterWords = 624;

// What a virtual draws from: the twister `n` into the object (`add rcx, n` with MSVC, `add rdi, n` with the Itanium ABI,
// then the call, or clang's tail jump), or the one a pointer `at` into the object points at (1.26.51: `mov rcx,
// [rcx + at]`, `xor edx, edx`, then the call; `mov rdi, [rdi + at]`, `xor esi, esi` on Linux; from 1.26.60, after a check
// of the thread it is drawn on, `this` kept in rbx, rsi or rdi: `mov rcx, [rsi + at]`), and the call's target
struct DrawsFrom {
    std::int64_t   in = -1, pointerAt = -1;
    std::uintptr_t generator                          = 0;
    bool           operator==(DrawsFrom const&) const = default;
};

DrawsFrom drawsFrom(std::uintptr_t at, int slot) {
    std::uintptr_t const fn = memory::virtualAt(at, slot);
    std::uint8_t         code[0x80];
    std::size_t          size = sizeof code;
    // a function at the end of the code has less after it
    while (fn && size >= 32 && !memory::safeRead(reinterpret_cast<void const*>(fn), code, size)) {
        size /= 2;
    }
    if (!fn || size < 32) {
        return {};
    }
    auto callAt = [&](std::size_t i) {
        std::int32_t rel;
        std::memcpy(&rel, code + i + 1, 4);
        return fn + i + 5 + rel;
    };
#ifdef _WIN32
    constexpr std::uint8_t This   = 1;  // rcx
    auto const             isCall = [](std::uint8_t op) { return op == 0xE8; };
    // xor edx, edx
    std::uint8_t const clearSecond[] = {0x31, 0xD2};
#else
    constexpr std::uint8_t This = 7;  // rdi
    auto const isCall = [](std::uint8_t op) { return op == 0xE8 || op == 0xE9; };
    // xor esi, esi
    std::uint8_t const clearSecond[] = {0x31, 0xF6};
#endif
    // add this, imm8
    for (std::size_t i = 0; i + 9 <= 32; ++i) {
        if (code[i] == 0x48 && code[i + 1] == 0x83 && code[i + 2] == (0xC0 | This) && isCall(code[i + 4]) && code[i + 3] && code[i + 3] < 0x80 && !(code[i + 3] % 8)) {
            return {code[i + 3], -1, callAt(i + 4)};
        }
    }
    // the registers that hold `this` (rbx 3, rsi 6, rdi 7: `mov reg, this`), then `mov this, [reg + disp32]`, then the
    // call (from 1.26.60.25 the draw takes no second argument to clear)
    unsigned holders = 1u << This;
    for (std::size_t i = 0; i + 14 <= size; ++i) {
        if (code[i] == 0x48 && code[i + 1] == 0x89 && (code[i + 2] & 0xF8) == (0xC0 | This << 3)) {
            holders |= 1u << (code[i + 2] & 7);
        }
        std::uint8_t const base = code[i + 2] & 7;
        if (code[i] == 0x48 && code[i + 1] == 0x8B && (code[i + 2] & 0xF8) == (0x80 | This << 3) && base != 4 && base != 5 && (holders >> base & 1)) {
            std::size_t const call = std::memcmp(code + i + 7, clearSecond, sizeof clearSecond) ? i + 7 : i + 9;
            if (isCall(code[call])) {
                std::int32_t pointerAt;
                std::memcpy(&pointerAt, code + i + 3, 4);
                return {-1, pointerAt, callAt(call)};
            }
        }
    }
    return {};
}

// whether `generator` is a Mersenne Twister's (Core::Random::_genRandInt32): its code holds the twister's matrix
// (0x9908B0DF) or its seeding multiplier (0x6C078965); other generators (a Level keeps more than one) draw otherwise and
// are seeded otherwise
bool isTwister(std::uintptr_t generator) {
    std::uint8_t code[0x400];
    if (!memory::safeRead(reinterpret_cast<void const*>(generator), code, sizeof code)) {
        return false;
    }
    std::uint8_t const matrix[] = {0xDF, 0xB0, 0x08, 0x99}, multiplier[] = {0x65, 0x89, 0x07, 0x6C};
    auto const         holds = [&](std::uint8_t const* word) { return std::search(code, code + sizeof code, word, word + 4) != code + sizeof code; };
    return holds(matrix) || holds(multiplier);
}

// A Random: an object whose IRandom::nextFloat and nextDouble (vtable slots 5 and 6 with MSVC; the Linux bindings name
// them) are Random's, which both draw from its twister (Core::Random::_genRandInt32); `in` is -1 when `at` is no
// Random. A twister pointed at is the Random's own one (`in` set).
DrawsFrom randomAt(std::uintptr_t at) {
    auto const& r    = bindings::current();
    int const   next = r.slot(n::RandomNextFloat) >= 0 ? r.slot(n::RandomNextFloat) : 5, nextDouble = r.slot(n::RandomNextDouble) >= 0 ? r.slot(n::RandomNextDouble) : 6;
    DrawsFrom   from = drawsFrom(at, next);
    if (from.generator == 0 || !(from == drawsFrom(at, nextDouble)) || !isTwister(from.generator)) {
        return {};
    }
    if (from.pointerAt >= 0) {
        std::uintptr_t const twister = readPtr(at, from.pointerAt);
        from.in                      = twister > at && twister - at < 0x40 ? static_cast<std::int64_t>(twister - at) : -1;
    }
    return from;
}

std::int64_t coreRandomAt(std::uintptr_t at) { return randomAt(at).in; }

// where the RTTI names the object's class (Linux), only a Random: other generators draw the same way
bool isRandomClass(std::uintptr_t object) {
    std::string const name = memory::classOf(object);
    return name.empty() || name == "6Random";
}

// a Random the Level owns: in it, or in what it points at (breadth first)
std::uintptr_t levelRandom(std::uintptr_t level) {
    std::unordered_set<std::uintptr_t> seen{level};
    std::queue<std::uintptr_t>         queue;
    queue.push(level);
    for (int visited = 0; !queue.empty() && visited < 20000; ++visited) {
        std::uintptr_t const object = queue.front();
        queue.pop();
        for (std::int64_t off = 0; off < (object == level ? 0x4000 : 0x400); off += 8) {
            std::uintptr_t const p = readPtr(object, off);
            if (inImage(p) && isRandomClass(object + off) && coreRandomAt(object + off) >= 0) {
                return object + off;
            }
            if (p >= 0x10000 && !(p & 7) && !inImage(p) && seen.size() < 200000 && seen.insert(p).second) {
                queue.push(p);
            }
        }
    }
    return 0;
}

// A Random of the agent's own, a copy of one the Level owns (or that one, `inPlace`), seeded by the agent with the one it
// copies, and a Randomize of the agent's own that draws from it
class OwnRandom {
public:
    bool make(std::uintptr_t level, bool inPlace, std::string& why) {
        // before 1.17.0 the Level's Random has no vtable to tell it by: the build says where its twister's seed is
        if (std::int64_t const seedAt = bindings::current().offset(n::LevelRandomSeed); inPlace && seedAt >= 0) {
            mRandom = reinterpret_cast<std::uint8_t*>(level);
            mSeedAt = seedAt;
            log::info("drops: the Level's Random's twister at Level +0x{:X}, seeded in place", seedAt);
            seed(1);
            return true;
        }
        std::uintptr_t const live = levelRandom(level);
        DrawsFrom const      from = live ? randomAt(live) : DrawsFrom{};
        mCoreAt                   = from.in;
        if (!live) {
            why = "no Random owned by the Level";
        } else if (!copy(live)) {
            why = "the Level's Random does not read";
        }
        if (!why.empty()) {
            return false;
        }
        mRandom = inPlace ? reinterpret_cast<std::uint8_t*>(live) : mCopy;
        mLive   = inPlace ? nullptr : reinterpret_cast<std::uint8_t*>(live);
        mSeedAt = mCoreAt + 8;
        // the copy's pointer to its twister: at its own
        if (from.pointerAt >= 0 && !inPlace) {
            std::uintptr_t const own = reinterpret_cast<std::uintptr_t>(mCopy) + mCoreAt;
            std::memcpy(mCopy + from.pointerAt, &own, 8);
        }
        // A Randomize refers to its Random through a NonOwnerPointer, which builds keep two ways: up to 1.21.2 a pointer to
        // where the Random's EnableNonOwnerReferences is (the Random + 8), from 1.21.20 a pointer to a byte that says it is
        // alive and the Random itself 0x10 in. This one says both: its pointer points at the Random + 8, whose low byte is
        // not zero (the Random is 16-aligned). Where the byte must be 1 (Linux from 1.26.0), the pointer points at a 1.
        bool const aliveIsOne      = bindings::current().variant(n::RandomizeAlive) == n::AliveIsOne;
        mSelf                      = aliveIsOne ? 1 : reinterpret_cast<std::uintptr_t>(mRandom) + 8;
        std::uintptr_t const where = reinterpret_cast<std::uintptr_t>(&mSelf), random = reinterpret_cast<std::uintptr_t>(mRandom);
        std::memcpy(mRandomize, &where, 8);
        std::memcpy(mRandomize + 0x10, &random, 8);
        std::string const placed = live - level < 0x4000 ? std::format("Level +0x{:X}", live - level) : std::format("0x{:X}, reached from the Level", live);
        log::info("drops: a Random of the Level at {}, its Core::Random +0x{:X}{}", placed, mCoreAt, inPlace ? ", seeded in place" : "");
        seed(1);
        return true;
    }

    // the twister's state for `value`, as Core::Random::_setSeed makes it (the words it initializes lazily as well), in
    // the Random and in the one it copies
    void seed(std::uint32_t value) {
        seed(mRandom, value);
        if (mLive) {
            seed(mLive, value);
        }
    }

    // what the drop code draws from: the Randomize, or from 1.26, where the drop code takes the IRandom itself (a Random
    // with its Core::Random right after its vtable), the Random
    void* argument() { return mCoreAt == 8 ? static_cast<void*>(mRandom) : static_cast<void*>(mRandomize); }

private:
    void seed(std::uint8_t* random, std::uint32_t value) {
        std::uint8_t* const twister = random + mSeedAt;
        std::uint32_t       word    = value;
        std::memcpy(twister, &value, 4);
        for (int i = 0; i < TwisterWords; ++i) {
            if (i) {
                word = 1812433253u * (word ^ (word >> 30)) + static_cast<std::uint32_t>(i);
            }
            std::memcpy(twister + TwisterWordsAt + 4 * i, &word, 4);
        }
        std::uint32_t const twistFirst = TwisterWords;
        std::memcpy(twister + TwisterIndexAt, &twistFirst, 4);
    }

    // as much of the Random as reads, up to the copy's size: its twister and the fields after it at least (a Random at the
    // end of the memory it is in has nothing readable after it)
    bool copy(std::uintptr_t live) {
        std::size_t const needed = static_cast<std::size_t>(mCoreAt) + 8 + TwisterIndexAt + 0x20;
        for (std::size_t size = sizeof mCopy; size >= needed; size -= 0x10) {
            if (memory::safeRead(reinterpret_cast<void const*>(live), mCopy, size)) {
                return true;
            }
        }
        return false;
    }

    alignas(16) std::uint8_t mCopy[0xC00]{};
    std::uint8_t*  mRandom = mCopy;
    std::uint8_t*  mLive   = nullptr;  // the Level's, which the copy is of
    std::uintptr_t mSelf   = 0;
    alignas(16) std::uint8_t mRandomize[0x20]{};
    std::int64_t mCoreAt = -1, mSeedAt = -1;
};

class Drawer {
public:
    Drawer(World const& world, ItemTypes const& items) {
        auto const& r   = bindings::current();
        mSpawnResources = r.address(n::BlockSpawnResources);
        mSetBlockXyz    = r.address(n::BlockSourceSetBlock);
        mSetBlockAt     = r.address(n::BlockSourceSetBlockAt);
        mChanger        = r.variant(n::SetBlockChanger) == n::ChangerContext ? mNoContext : nullptr;
        mSpawnKind      = r.variant(n::SpawnResourcesKind);
        mOnRemoveSlot   = r.slot(n::BlockOnRemove);
        mContext.tool   = reinterpret_cast<std::uintptr_t>(mEmptyHand);
        mContext.region = world.overworld;
        for (ItemType const& item : items.types) {
            mItems.emplace(item.address, item.name);
        }
    }

    // the hook stays: it spawns as the game would whenever the agent is not drawing
    ~Drawer() { gSpawned = nullptr; }

    // which drop code the build has, what it reads by content, where blocks are set; then where its stacks are
    bool make(World const& world, std::vector<BlockType> const& types, std::string& why) {
        auto const air             = std::ranges::find(types, std::string("minecraft:air"), &BlockType::name);
        mAir                       = air == types.end() ? 0 : air->states[air->defaultState];
        bool const levelRandomOnly = mSpawnResources && (mSpawnKind == n::SpawnBlockLevelRandom || mSpawnKind == n::SpawnBlockLevelRandomTool);
        if (!mRandom.make(world.level, levelRandomOnly, why)) {
            return false;
        }
        gBlockDataAt = blockDataOffset(types);
        if (gBlockDataAt < 0) {
            why = "no Block::mData";
            return false;
        }
        mActorTypeAt        = blockActorTypeAt(types);
        int const boundSlot = bindings::current().slot(n::BlockSourceSetBlockSlot);
        mSetBlockSlot       = mSetBlockXyz || mSetBlockAt ? -1 : boundSlot >= 0 ? boundSlot : setBlockSlot(mContext.region);
        if (!mSetBlockXyz && !mSetBlockAt && mSetBlockSlot < 0) {
            why = "no BlockSource::setBlock";
        } else if (mActorTypeAt < 0) {
            why = "no BlockLegacy::mBlockEntityType";
        } else if (!loadedPosition(types)) {
            why        = std::format("no loaded chunk around {} {} {} to set blocks in", DrawnAt.x, DrawnAt.y, DrawnAt.z);
            mNotLoaded = true;
        }
        if (!why.empty()) {
            return false;
        }
        if (mSpawnResources) {
            return hookSpawnItem(why);
        }
        auto const stone = std::ranges::find(types, std::string("minecraft:stone"), &BlockType::name);
        if (stone == types.end() || !dropStrategy(stone->address)) {
            why = "stone has no drop strategy";
        } else if (!calibrate(stone->address, stone->states[stone->defaultState])) {
            why = "stone's drops do not read as one item stack";
        }
        return why.empty();
    }

    bool notLoaded() const { return mNotLoaded; }

    // Block::getMapColor: the bound function, else the one short function calling a stored-colour slot of the BlockLegacy
    // through a Block's mLegacy that answers the known blocks' colours, else (no such function) its parts where it is
    // inline in its callers, asked as they ask them, where they answer the known colours
    bool findMapColor(std::vector<BlockType> const& types, std::string& why) {
        auto stateOf = [&](char const* name) {
            auto const it = std::ranges::find(types, std::string(name), &BlockType::name);
            return it == types.end() ? std::uintptr_t{0} : it->states[it->defaultState];
        };
        // MaterialColor's: stone, dirt, sand, the black of obsidian
        std::pair<std::uintptr_t, int> const known[] = {
            {stateOf("minecraft:stone"), 0x707070}, {stateOf("minecraft:dirt"), 0x976D4D}, {stateOf("minecraft:sand"), 0xF7E9A3}, {stateOf("minecraft:obsidian"), 0x191919}};
        if (std::ranges::any_of(known, [](auto const& k) { return k.first == 0; })) {
            why = "a block of known map colour is missing";
            return false;
        }
        // whether `ask` answers each known colour; where it does not, the log says what it answered instead
        auto answers = [&](std::string const& what, Ask const& ask) {
            return std::ranges::all_of(known, [&](auto const& k) {
                Color4 c{};
                set(mPos, mAir);
                std::string wrong;
                if (!set(mPos, k.first)) {
                    wrong = "the block is not set";
                } else if (!ask(k.first, &mPos, &c)) {
                    wrong = std::format("it faults: {}", lastFault());
                } else {
                    float const expected[] = {((k.second >> 16) & 0xFF) / 255.0f, ((k.second >> 8) & 0xFF) / 255.0f, (k.second & 0xFF) / 255.0f, 1.0f};
                    for (int i = 0; i < 4 && wrong.empty(); ++i) {
                        if (std::abs(c.c[i] - expected[i]) > 1e-6f) {
                            wrong = std::format("it answers {} {} {} {}", c.c[0], c.c[1], c.c[2], c.c[3]);
                        }
                    }
                }
                if (!wrong.empty()) {
                    log::info("map colours: {} on #{:06X}: {}", what, k.second, wrong);
                }
                return wrong.empty();
            });
        };
        auto function = [this](std::uintptr_t fn) {
            return Ask([this, fn](std::uintptr_t state, BlockPos const* pos, Color4* c) { return callMapColor(fn, state, mContext.region, pos, c); });
        };
        auto answersAt = [&](std::uintptr_t fn) { return answers(std::format("0x{:X}", fn - memory::imageBase()), function(fn)); };
        // the getMapColor slot (the first of those that fit through which one function answers the known colours), and the
        // function most types have there: a type that has it reads no block from the world
        int const                   boundSlot = bindings::current().slot(n::BlockGetMapColorSlot);
        auto const                  slots     = boundSlot >= 0 ? std::vector<int>{boundSlot} : mapColorSlots(types);
        std::int64_t const          legacy    = legacyAt(types);
        std::uintptr_t const        bound     = bindings::current().address(n::BlockGetMapColor);
        std::vector<std::uintptr_t> found;
        int                         slot = boundSlot;
        if (bound) {
            found = answersAt(bound) ? std::vector{bound} : std::vector<std::uintptr_t>{};
        }
        for (int candidate : slot >= 0 && bound ? std::vector<int>{} : slots) {
            auto const  callers = legacy < 0 ? std::vector<std::uintptr_t>{} : callersOf({candidate}, legacy);
            std::string listed;
            for (std::uintptr_t fn : callers) {
                listed += std::format(" 0x{:X}", fn - memory::imageBase());
            }
            log::info("map colours: slot {}, through Block::mLegacy at +{}, is called by{}", candidate, legacy, listed.empty() ? " none" : listed);
            if (bound) {
                // the slot is the one the bound function calls
                if (std::ranges::find(callers, bound) != callers.end() || candidate == slots.back()) {
                    slot = candidate;
                    break;
                }
                continue;
            }
            std::vector<std::uintptr_t> answering;
            for (std::uintptr_t fn : callers) {
                if (answersAt(fn)) {
                    answering.push_back(fn);
                }
            }
            if (answering.size() == 1) {
                found = answering;
                slot  = candidate;
                break;
            }
        }
        Ask         ask;
        std::string inlineWhy;
        if (found.size() == 1) {
            ask = function(found[0]);
            log::info("map colours: Block::getMapColor at rva 0x{:X}", found[0] - memory::imageBase());
        } else if (found.empty() && !bound && legacy >= 0) {
            // no function: Block::getMapColor is inline in its callers
            for (int candidate : slots) {
                auto const parts = inlinedMapColor(legacy, candidate, inlineWhy);
                if (!parts) {
                    log::info("map colours: {}", inlineWhy);
                    continue;
                }
                Ask asked = [this, p = *parts](std::uintptr_t state, BlockPos const* pos, Color4* c) { return askMapColor(p, state, mContext.region, pos, c); };
                if (answers("the inline parts", asked)) {
                    ask  = std::move(asked);
                    slot = candidate;
                    logComponents(types, *parts);
                    break;
                }
                inlineWhy = std::format("the parts inline at slot {} do not answer the known colours", candidate);
            }
        }
        std::uintptr_t inherited = 0;
        if (slot >= 0) {
            std::map<std::uintptr_t, int> count;
            for (BlockType const& type : types) {
                ++count[memory::virtualAt(type.address, slot)];
            }
            inherited = std::ranges::max_element(count, {}, [](auto const& e) { return e.second; })->first;
        }
        // kept for the later calls: by value
        auto ownColor = [slot, inherited](BlockType const& type) { return memory::virtualAt(type.address, slot) != inherited; };
        set(mPos, mAir);
        if (!ask) {
            why = std::format("{} functions answer the known blocks' map colours as Block::getMapColor{}", found.size(), inlineWhy.empty() ? "" : ", and " + inlineWhy);
            return false;
        }
        if (slot < 0) {
            why = "no slot of BlockLegacy::getMapColor";
            return false;
        }
        mMapColor = std::move(ask);
        mOwnColor = ownColor;
        return true;
    }

    // the types whose default state has a map colour component (which the inline parts tint), in the log
    static void logComponents(std::vector<BlockType> const& types, MapColorParts const& parts) {
        std::vector<std::string> named;
        for (BlockType const& type : types) {
            if (mapColorComponent(parts, type.states[type.defaultState])) {
                named.push_back(type.name);
            }
        }
        std::string listed;
        for (std::size_t i = 0; i < named.size() && i < 12; ++i) {
            listed += " " + named[i];
        }
        log::info("map colours: {} types have a map colour component:{}", named.size(), listed);
    }

    // Each state's map colour, as Block::getMapColor answers with the state set where blocks are set. A sub-chunk keeps a
    // palette of every state set in it, which takes no more than 4096 (setting air between leaves it there): each
    // PerSubChunk states go in the sub-chunk above. False (`why`) when a call faults, or a state of a type with a map
    // colour of its own (one that may read the block from the world) is not set.
    bool mapColors(std::vector<BlockType> const& types, std::vector<std::vector<MapColor>>& out, std::string& why) {
        constexpr std::size_t PerSubChunk = 2048;
        BlockPos              at{mPos.x, mPos.y + 16, mPos.z};
        std::size_t           inSubChunk = 0;
        for (BlockType const& type : types) {
            auto& states = out.emplace_back();
            for (std::uintptr_t state : type.states) {
                if (++inSubChunk > PerSubChunk) {
                    set(at, mAir);
                    at.y += 16;
                    inSubChunk = 1;
                }
                Color4 c{};
                set(at, mAir);
                // a block the world does not take where it is set (soul fire on air): asked as it is, where its type's
                // map colour is the stored one
                if (!set(at, state) && mOwnColor(type)) {
                    log::info("map colours: {} #{} is not set", type.name, states.size());
                }
                if (!mMapColor(state, &at, &c)) {
                    why = std::format("Block::getMapColor faults on {}: {}", type.name, lastFault());
                    set(at, mAir);
                    return false;
                }
                states.push_back({c.c[0], c.c[1], c.c[2], c.c[3]});
            }
        }
        set(at, mAir);
        return true;
    }

    // whether the agent can ask the game for the type's drops: data-driven types have no strategy (their loot table drops)
    bool drawable(BlockType const& type) const { return mSpawnResources || dropStrategy(type.address); }

    // `samples` draws for `state`, from a Random seeded with `seed`; nullopt, with `why`, when a call faults or reads wrong
    std::optional<StateDrops> draw(BlockType const& type, std::uintptr_t state, std::uint32_t seed, int samples, std::string& why) {
        if (keepsActor(type) && !setForItsActor(type, state, why)) {
            return std::nullopt;
        }
        mRandom.seed(seed);
        std::map<std::tuple<std::string, int, int>, Drop> found;
        std::vector<Stack>                                stacks;
        for (int i = 0; i < samples && why.empty(); ++i) {
            stacks.clear();
            if (!once(type, state, stacks, why)) {
                break;
            }
            std::map<std::tuple<std::string, int, int>, int> counts;
            for (Stack const& stack : stacks) {
                auto const name = mItems.find(stack.item);
                // an empty stack drops nothing
                if (!stack.item || !stack.count) {
                    continue;
                }
                if (name == mItems.end()) {
                    why = std::format("a stack of {} of an Item 0x{:X} the registry does not have", stack.count, stack.item);
                } else {
                    counts[{name->second, stack.aux, stack.data}] += stack.count;
                }
            }
            for (auto const& [key, count] : counts) {
                auto const& [item, aux, data] = key;
                auto [it, fresh]              = found.try_emplace(key, Drop{item, aux, data, count, count, 0});
                it->second.minCount           = std::min(it->second.minCount, count);
                it->second.maxCount           = std::max(it->second.maxCount, count);
                ++it->second.hits;
            }
        }
        if (!why.empty()) {
            return std::nullopt;
        }
        StateDrops out{{}, samples};
        for (auto& [key, drop] : found) {
            out.drops.push_back(std::move(drop));
        }
        return out;
    }

private:
    // Where blocks are set, and the drop code is told the block is: the overworld's chunks around the one a ticking area
    // keeps loaded (DrawnAt; spiralling out), at y 64, the first it has loaded. A block there is not both stone and air,
    // so setting one of them changes it; then setting stone twice changes it once.
    bool loadedPosition(std::vector<BlockType> const& types) {
        auto const stone = std::ranges::find(types, std::string("minecraft:stone"), &BlockType::name);
        if (stone == types.end() || !mAir) {
            return false;
        }
        std::uintptr_t const block = stone->states[stone->defaultState];
        for (int ring = 0; ring <= 32; ++ring) {
            for (int cx = -ring; cx <= ring; ++cx) {
                for (int cz = -ring; cz <= ring; ++cz) {
                    if (std::max(std::abs(cx), std::abs(cz)) != ring) {
                        continue;
                    }
                    BlockPos const pos{DrawnAt.x + cx * 16, DrawnAt.y, DrawnAt.z + cz * 16};
                    if (!set(pos, block) && !set(pos, mAir)) {
                        continue;
                    }
                    set(pos, mAir);
                    if (!set(pos, block) || set(pos, block)) {
                        return false;
                    }
                    mPos                  = pos;
                    mContext.x            = pos.x;
                    mContext.y            = pos.y;
                    mContext.z            = pos.z;
                    std::string const how = mSetBlockXyz || mSetBlockAt ? "" : std::format(" (BlockSource::setBlock at slot {})", mSetBlockSlot);
                    log::info("drops: blocks are set at {} {} {}{}", pos.x, pos.y, pos.z, how);
                    return true;
                }
            }
        }
        return false;
    }

    bool set(BlockPos const& pos, std::uintptr_t block) {
        if (mSetBlockXyz) {
            return callSetBlockXyz(mSetBlockXyz, mContext.region, &pos, block);
        }
        return callSetBlockAt(mSetBlockAt ? mSetBlockAt : memory::virtualAt(mContext.region, mSetBlockSlot), mContext.region, &pos, block, mChanger);
    }

    bool keepsActor(BlockType const& type) const { return read<std::uint8_t>(type.address, mActorTypeAt) != 0; }

    // A type that keeps a block actor spawns its drops from it, so its default state is set where its states are drawn
    // (air first: no actor of another type stays): one actor a type, as the world refuses block actors after about 4096
    // in a tick. Where onRemove is asked, which reads the block from the world, the state itself.
    bool setForItsActor(BlockType const& type, std::uintptr_t state, std::string& why) {
        std::uintptr_t const placed = mOnRemoveSlot >= 0 ? state : type.states[type.defaultState];
        if (mPlaced == placed) {
            return true;
        }
        if (mActorOf != type.address) {
            set(mPos, mAir);
        }
        mActorOf = type.address;
        mPlaced  = placed;
        if (!set(mPos, placed)) {
            why = "BlockSource::setBlock does not set it";
        }
        return why.empty();
    }

    // the stacks one mining of `state` drops
    bool once(BlockType const& type, std::uintptr_t state, std::vector<Stack>& out, std::string& why) {
        if (mSpawnResources) {
            gSpawned = &out;
            if (!callSpawnResources(mSpawnResources, mSpawnKind, state, mContext.region, &mPos, mRandom.argument(), mEmptyHand, &mContext)) {
                why = "Block::spawnResources faults: " + lastFault();
            } else if (out.empty() && mOnRemoveSlot >= 0 && keepsActor(type) &&
                       !callOnRemove(memory::virtualAt(type.address, mOnRemoveSlot), type.address, mContext.region, &mPos)) {
                why = "BlockLegacy::onRemove faults: " + lastFault();
            }
            gSpawned = nullptr;
            return why.empty();
        }
        Stacks stacks;
        if (!callStrategy(dropStrategy(type.address), &stacks, state, mRandom.argument(), &mContext)) {
            why = "the drop strategy faults: " + lastFault();
        } else if (stacks.end < stacks.begin || (stacks.end - stacks.begin) % mStride) {
            why = std::format("{} bytes of stacks", stacks.end - stacks.begin);
        }
        for (std::uintptr_t at = stacks.begin; why.empty() && at < stacks.end; at += mStride) {
            out.push_back(stackAt(at));
        }
        if (why.empty() && !release(stacks)) {
            why = "a stack's destructor faults";
        }
        return why.empty();
    }

    bool hookSpawnItem(std::string& why) {
        auto* const target = reinterpret_cast<void*>(bindings::current().address(n::SpawnerSpawnItem));
        if (!target) {
            why = "no Spawner::spawnItem";
        } else if (!memory::hook(target, reinterpret_cast<void*>(&hookedSpawnItem), reinterpret_cast<void**>(&gSpawnItem))) {
            why = "could not hook Spawner::spawnItem";
        }
        return why.empty();
    }

    // the stride of the ItemStacks in the vector: what one stack of `oneStack`'s drops takes
    bool calibrate(std::uintptr_t type, std::uintptr_t oneStack) {
        Stacks stacks;
        mRandom.seed(1);
        if (!callStrategy(dropStrategy(type), &stacks, oneStack, mRandom.argument(), &mContext)) {
            return false;
        }
        std::uintptr_t const size = stacks.end - stacks.begin;
        bool const           one  = size >= 0x40 && size <= 0x200 && !(size % 8);
        mStride                   = one ? size : 0;
        if (one) {
            keepStackVtable(stacks.begin);
        }
        release(stacks);
        return one;
    }

    bool release(Stacks const& stacks) {
        bool ok = true;
        for (std::uintptr_t at = stacks.begin; at < stacks.end; at += mStride) {
            ok = destroyStack(at) && ok;
        }
#ifdef _WIN32
        // the agent shares the game's CRT heap (/MD)
        if (stacks.begin) {
            std::allocator<char>().deallocate(reinterpret_cast<char*>(stacks.begin), stacks.capacity - stacks.begin);
        }
#else
        // the game's own operator delete: the buffer is its standard library's
        if (std::uintptr_t const free = bindings::current().address(n::OperatorDelete); free && stacks.begin) {
            reinterpret_cast<void (*)(void*)>(free)(reinterpret_cast<void*>(stacks.begin));
        }
#endif
        return ok;
    }

    alignas(16) std::uint8_t mEmptyHand[0x100]{};
    alignas(16) std::uint8_t mNoContext[0x40]{};  // a BlockChangeContext of none
    void const*                           mChanger = nullptr;
    OwnRandom                             mRandom;
    std::uintptr_t                        mSpawnResources = 0, mSetBlockXyz = 0, mSetBlockAt = 0, mAir = 0, mActorOf = 0, mPlaced = 0, mStride = 0;
    std::int64_t                          mActorTypeAt = -1;
    int                                   mSpawnKind = 0, mOnRemoveSlot = -1, mSetBlockSlot = -1;
    bool                                  mNotLoaded = false;
    BlockPos                              mPos;       // where blocks are set (loadedPosition)
    Ask                                   mMapColor;  // Block::getMapColor (findMapColor)
    std::function<bool(BlockType const&)> mOwnColor;  // whether a type has a map colour of its own
    Context                               mContext;
    std::map<std::uintptr_t, std::string> mItems;  // Item* -> name
};

// why the default states' drops are not those the game is known for; "" when they are
std::string wrongKnownDrops(std::vector<BlockType> const& types, std::vector<std::vector<StateDrops>> const& drops) {
    auto of = [&](char const* name) -> StateDrops const* {
        for (std::size_t i = 0; i < types.size(); ++i) {
            if (types[i].name == name) {
                return &drops[i][types[i].defaultState];
            }
        }
        return nullptr;
    };
    auto always = [](StateDrops const* d, char const* item) {
        return d && d->drops.size() == 1 && d->drops[0].item == item && d->drops[0].hits == d->samples && d->drops[0].maxCount == 1;
    };
    auto sometimes = [](StateDrops const* d, char const* item) {
        return d && std::ranges::any_of(d->drops, [&](Drop const& x) { return x.item == item && x.hits > 0 && x.hits < d->samples; });
    };
    auto said = [](StateDrops const* d) {
        std::string out;
        if (d) {
            for (Drop const& x : d->drops) {
                out += std::format(" {}:{} {}-{} ({}/{})", x.item, x.aux, x.minCount, x.maxCount, x.hits, d->samples);
            }
        }
        return out.empty() ? std::string(" nothing") : out;
    };
    StateDrops const *stone = of("minecraft:stone"), *dirt = of("minecraft:dirt"), *coal = of("minecraft:coal_ore"), *glass = of("minecraft:glass"),
                     *gravel = of("minecraft:gravel");
    if (!always(stone, "minecraft:cobblestone")) {
        return "stone drops" + said(stone);
    }
    if (!always(dirt, "minecraft:dirt")) {
        return "dirt drops" + said(dirt);
    }
    if (!always(coal, "minecraft:coal")) {
        return "coal ore drops" + said(coal);
    }
    if (!glass || !glass->drops.empty()) {
        return "glass drops" + said(glass);
    }
    if (!sometimes(gravel, "minecraft:flint")) {
        return "gravel drops" + said(gravel);
    }
    return "";
}

std::uint32_t nameSeed(std::string const& name) {
    std::uint32_t hash = 0x811c9dc5;
    for (char c : name) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 0x01000193;
    }
    return hash;
}

}

AllDrops drops(World const& world, ItemTypes const& items, std::vector<BlockType> const& types) {
    AllDrops drops;
    auto     drawer = std::make_unique<Drawer>(world, items);
    if (!drawer->make(world, types, drops.error)) {
        drops.notLoaded = drawer->notLoaded();
        return drops;
    }
    int undrawn = 0;
    for (BlockType const& type : types) {
        // about 8192 draws a type, at least 64 a state: a drop as rare as an oak's apple (1 in 200) is drawn
        int const samples = std::max<int>(64, static_cast<int>(8192 / type.states.size()));
        auto&     states  = drops.types.emplace_back();
        if (!drawer->drawable(type)) {
            states.assign(type.states.size(), StateDrops{{}, 0});
            ++undrawn;
            continue;
        }
        for (std::uintptr_t state : type.states) {
            std::string why;
            auto        drawn = drawer->draw(type, state, nameSeed(type.name), samples, why);
            if (!drawn) {
                drops.error = std::format("the drops of {} #{}: {}", type.name, states.size(), why);
                drops.types.clear();
                return drops;
            }
            states.push_back(std::move(*drawn));
        }
    }
    drops.error = wrongKnownDrops(types, drops.types);
    if (drops.error.empty() && drawer->findMapColor(types, drops.error)) {
        drawer->mapColors(types, drops.mapColors, drops.error);
    }
    if (!drops.error.empty()) {
        drops.types.clear();
        drops.mapColors.clear();
        return drops;
    }
    log::info("drops: {} block types drawn, {} left to their loot tables", drops.types.size() - undrawn, undrawn);
    return drops;
}

}
