#include "game/World.h"

#include "bindings/Bindings.h"
#include "bindings/Names.h"
#include "core/Log.h"
#include "game/Registry.h"
#include "memory/Guard.h"
#include "memory/Mem.h"

namespace bdg::game {

namespace n = bindings::names;
using memory::imageBase;
using memory::inImage;
using memory::readPtr;
using memory::virtualAt;

namespace {

bindings::Registry const& r() { return bindings::current(); }

// A Level's vtable is in the image. When getDimension is bound, the vtable must also hold it at its slot.
bool looksLikeLevel(std::uintptr_t obj) {
    if (obj <= 0x10000) {
        return false;
    }
    std::uintptr_t vt = readPtr(obj);
    if (!inImage(vt)) {
        return false;
    }
    std::uintptr_t want = r().address(n::LevelGetDimension);
    int            slot = r().slot(n::LevelGetDimensionSlot);
    return want && slot >= 0 ? readPtr(vt, slot * 8) == want : true;
}

std::uintptr_t validDimension(std::uintptr_t dim) { return dim > 0x10000 && !inImage(dim) && inImage(readPtr(dim)) ? dim : 0; }

// A real BlockSource: a heap object other than the Dimension and Level, whose vtable is in the image, and which
// keeps references to its Level and Dimension among its first members (mLevel, mChunkSource, mDimension); an
// object that only has a vtable (a brightness ramp, a weather) does not.
bool looksLikeBlockSource(std::uintptr_t bs, std::uintptr_t dim, std::uintptr_t level) {
    if (bs <= 0x10000 || inImage(bs) || bs == dim || bs == level) {
        return false;
    }
    if (!inImage(readPtr(bs))) {
        return false;
    }
    bool hasDim = false, hasLevel = !level;
    for (std::int64_t off = 8; off < 0x80; off += 8) {
        std::uintptr_t v = readPtr(bs, off);
        hasDim |= v == dim;
        hasLevel |= v == level;
    }
    return hasDim && hasLevel;
}

std::uintptr_t callNoArgs(std::uintptr_t fn, std::uintptr_t self) {
    std::uintptr_t out = 0;
    if (!memory::guard([&] { out = reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(fn)(self); })) {
        out = 0;
    }
    return out;
}

// the getter slot, then the bound member offset (builds where the scan finds a lookalike first), then the first
// BlockSource-like member
std::uintptr_t blockSourceOf(std::uintptr_t dim, std::uintptr_t level) {
    if (std::uintptr_t fn = r().address(n::DimensionBlockSourceGetter)) {
        std::uintptr_t bs = callNoArgs(fn, dim);
        if (looksLikeBlockSource(bs, dim, level)) {
            log::info("overworld BlockSource from Dimension::getBlockSourceFromMainChunkSource");
            return bs;
        }
    }
    if (std::uintptr_t fn = virtualAt(dim, r().slot(n::DimensionGetBlockSource))) {
        std::uintptr_t bs = callNoArgs(fn, dim);
        if (looksLikeBlockSource(bs, dim, level)) {
            log::info("overworld BlockSource from the getter slot");
            return bs;
        }
    }
    if (std::int64_t off = r().offset(n::DimensionBlockSource); off >= 0) {
        std::uintptr_t bs = readPtr(dim, off);
        if (looksLikeBlockSource(bs, dim, level)) {
            log::info("overworld BlockSource at Dimension::mBlockSource (+0x{:X})", off);
            return bs;
        }
        log::info("Dimension::mBlockSource (+0x{:X}) is not a BlockSource: vtable rva 0x{:X}", off, bs > 0x10000 ? readPtr(bs) - imageBase() : 0);
    }
    for (std::int64_t off = 0x80; off <= 0x200; off += 8) {
        std::uintptr_t bs = readPtr(dim, off);
        if (looksLikeBlockSource(bs, dim, level)) {
            log::info("overworld BlockSource at Dimension+0x{:X}: vtable rva 0x{:X}", off, readPtr(bs) - imageBase());
            return bs;
        }
    }
    return 0;
}

// DimensionByPointer: Dimension* f(Level*, DimensionType) in rax.
// DimensionByWeakRef: WeakRef<Dimension> f(Level*, DimensionType) by value, through a hidden pointer: MSVC passes it
// after `this` (rcx = this, rdx = &ret, r8 = dim), the Itanium ABI before (rdi = &ret, rsi = this, edx = dim); the
// weak_ptr's first qword is the Dimension*.
std::uintptr_t callDimensionGetter(std::uintptr_t fn, std::uintptr_t level, int dimId) {
    if (!fn) {
        return 0;
    }
    std::uintptr_t dim      = 0;
    bool const     returned = memory::guard([&] {
        if (r().variant(n::DimensionKind) == n::DimensionByWeakRef) {
            std::uintptr_t ref[2] = {0, 0};
#ifdef _WIN32
            reinterpret_cast<void* (*)(std::uintptr_t, std::uintptr_t*, int)>(fn)(level, ref, dimId);
#else
            reinterpret_cast<void* (*)(std::uintptr_t*, std::uintptr_t, int)>(fn)(ref, level, dimId);
#endif
            dim = ref[0];
        } else {
            dim = reinterpret_cast<std::uintptr_t (*)(std::uintptr_t, int)>(fn)(level, dimId);
        }
    });
    return returned ? validDimension(dim) : 0;
}

// The real Level behind a Level::tick `this`. On some builds `this` is a holder object with the Level among its
// first members; this finds it structurally, without calling anything.
std::uintptr_t resolveLevel(std::uintptr_t self) {
    if (looksLikeLevel(self)) {
        return self;
    }
    if (r().has(n::LevelGetDimension)) {
        for (std::int64_t off = 0; off <= 0x80; off += 8) {
            std::uintptr_t candidate = readPtr(self, off);
            if (candidate > 0x10000 && !inImage(candidate) && looksLikeLevel(candidate)) {
                return candidate;
            }
        }
    }
    // The bound getDimension slot is the ILevel subobject's; a more-derived vtable at +0 may hold it elsewhere.
    // Any in-image vtable is accepted here: the dimension -> BlockSource chain validates later.
    return inImage(readPtr(self)) ? self : 0;
}

// Level -> overworld Dimension -> its main BlockSource. Only a route whose whole chain validates is taken.
std::uintptr_t overworldBlockSource(std::uintptr_t level) {
    if (level < 0x10000) {
        return 0;
    }
    auto blockSourceOfDimension = [&](std::uintptr_t dim) { return dim ? blockSourceOf(dim, level) : 0; };
    // the creators first, so the overworld is created rather than waited for
    bool const  weakRef   = r().variant(n::DimensionKind) == n::DimensionByWeakRef;
    char const* getters[] = {weakRef ? n::LevelGetOrCreateDimension : n::LevelCreateDimension, n::LevelGetDimension};
    for (char const* getter : getters) {
        if (std::uintptr_t bs = blockSourceOfDimension(callDimensionGetter(r().address(getter), level, 0))) {
            return bs;
        }
    }
    // then the vtable around the bound getDimension slot: the slot before it first, which on every build with a PDB
    // is the creator (createDimension, getOrCreateDimension from 1.19.50), so the overworld is created if it does
    // not exist yet
    int getDimension = r().slot(n::LevelGetDimensionSlot);
    if (getDimension < 1) {
        return 0;
    }
    for (int offset : {-1, 0, -2, 1, -3, 2, 3, -4}) {
        int slot = getDimension + offset;
        if (slot < 1) {
            continue;
        }
        if (std::uintptr_t bs = blockSourceOfDimension(callDimensionGetter(virtualAt(level, slot), level, 0))) {
            log::info("overworld Dimension from Level vtable slot {}", slot);
            return bs;
        }
    }
    return 0;
}

}

std::optional<World> findWorld(std::uintptr_t tickSelf) {
    World world{resolveLevel(tickSelf), 0, 0};
    if (!world.level) {
        return std::nullopt;
    }
    world.blockTypeMap = blockTypeMap(world.level);
    if (!world.blockTypeMap) {
        return std::nullopt;
    }
    world.overworld = overworldBlockSource(world.level);
    if (!world.overworld) {
        return std::nullopt;
    }
    log::info("level 0x{:X}, block type map 0x{:X}, overworld BlockSource 0x{:X}", world.level, world.blockTypeMap, world.overworld);
    return world;
}

}
