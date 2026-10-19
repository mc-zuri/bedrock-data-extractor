#include "game/Registry.h"

#include "bindings/Bindings.h"
#include "bindings/Names.h"
#include "core/Log.h"
#include "memory/Guard.h"
#include "memory/Mem.h"

#include <format>

namespace bdg::game {

namespace n = bindings::names;
using memory::inImage;
using memory::readPtr;
using memory::virtualAt;

namespace {

bindings::Registry const& r() { return bindings::current(); }

// A node's key is at memory::TreeValue (0x20), the mapped value right after it: after a HashedString key (MSVC 0x50,
// libc++ 0x48), after the std::string key of older builds (MSVC 0x40). The clang-built Windows servers use MSVC's STL.
bool hashedKeys() { return r().variant(n::BlockMapKey, n::MapKeyHashedString) == n::MapKeyHashedString; }

std::int64_t mappedAt() { return memory::TreeValue + static_cast<std::int64_t>(hashedKeys() ? memory::hashedStringSize() : memory::stringSize()); }

bool mapReady(std::uintptr_t map) {
    if (map < 0x10000) {
        return false;
    }
    std::uint64_t const size = memory::treeSize(map);
    return memory::treeRoot(map) && size > 400 && size < 3000;
}

// Stronger than mapReady: the map really holds block types (air, and mostly "minecraft:" names).
bool isBlockMap(std::uintptr_t map) {
    if (!mapReady(map)) {
        return false;
    }
    bool air   = false;
    int  total = 0, vanilla = 0;
    forEachBlockType(map, [&](std::string const& name, std::uintptr_t) {
        ++total;
        if (name == "minecraft:air") {
            air = true;
        }
        if (name.rfind("minecraft:", 0) == 0) {
            ++vanilla;
        }
    });
    return air && total > 400 && vanilla * 5 >= total * 4;
}

std::uintptr_t mapViaMember(std::uintptr_t level) {
    std::int64_t off = r().offset(n::LevelBlockTypeRegistry);
    if (off < 0) {
        return 0;
    }
    std::uintptr_t p = readPtr(level, off);
    if (p < 0x10000) {
        return 0;
    }
    // the member is the registry itself or a NonOwnerPtr control block pointing at it; the map is at +0
    for (std::uintptr_t reg : {p, readPtr(p)}) {
        if (isBlockMap(reg)) {
            return reg;
        }
    }
    return 0;
}

// Level::getBlockTypeRegistry returns a 24-byte NotNullNonOwnerPtr<BlockTypeRegistry>. MSVC servers
// return it through a hidden buffer (rcx=this, rdx=&ret), registry at [2]; clang-built servers return
// the pointer in rax. The wrong convention corrupts the stack past SEH, so it is a binding, not a probe.
std::uintptr_t mapViaGetter(std::uintptr_t level) {
    std::uintptr_t fn = virtualAt(level, r().slot(n::LevelGetBlockTypeRegistry));
    if (!fn) {
        return 0;
    }
    alignas(16) std::uintptr_t out[4]   = {0, 0, 0, 0};
    bool const                 returned = memory::guard([&] {
        if (r().variant(n::RegistryGetterKind) == n::RegistryInRax) {
            out[2] = reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(fn)(level);
        } else {
            reinterpret_cast<void* (*)(std::uintptr_t, std::uintptr_t*)>(fn)(level, out);
        }
    });
    if (!returned) {
        return 0;
    }
    for (std::uintptr_t reg : {out[2], out[0], out[1], out[3]}) {
        if (reg >= 0x10000 && isBlockMap(reg)) {
            return reg;
        }
    }
    return 0;
}

void logRejectedMap(char const* route, std::uintptr_t map) {
    std::uintptr_t const root = memory::treeRoot(map);
    log::info("{}: map 0x{:X} rejected: root 0x{:X} size {}", route, map, root, memory::treeSize(map));
    std::uintptr_t node = root;
    for (int i = 0; i < 3 && node; ++i, node = readPtr(node, memory::treeLayout().left)) {
        std::string raw;
        for (int b = 0; b < 0x60; b += 8) {
            raw += std::format("{:016X} ", readPtr(node, memory::TreeValue + b));
        }
        log::info("  node 0x{:X}: +0x20.. {} | string '{}' | hashed '{}'", node, raw, memory::readString(node + memory::TreeValue),
                  memory::readHashedString(node + memory::TreeValue));
    }
}

std::uintptr_t mapViaStatic() {
    std::uintptr_t map = r().address(n::BlockLookupMap);
    if (!map) {
        return 0;
    }
    if (isBlockMap(map)) {
        return map;
    }
    logRejectedMap("static map", map);
    return 0;
}

// Without a binding (Linux builds without symbols): the static map in the image's data, which holds about as many
// block types as a map of them can (read directly: the data is mapped), else one the Level reaches, as a member or
// through the pointer a member holds (the registry or its NonOwnerPointer's control block, the map at its +0).
std::uintptr_t mapByContent(std::uintptr_t level) {
    auto const layout = memory::treeLayout();
    for (auto const& [from, to] : memory::dataSections()) {
        for (std::uintptr_t at = (from + 7) & ~std::uintptr_t{7}; at + 0x20 <= to; at += 8) {
            std::uint64_t const size = *reinterpret_cast<std::uint64_t const*>(at + layout.sizeAt);
            if (size > 400 && size < 3000 && isBlockMap(at)) {
                log::info("block type map in the image's data at rva 0x{:X}", at - memory::imageBase());
                return at;
            }
        }
    }
    for (std::int64_t off = 0; off < 0x4000; off += 8) {
        std::uintptr_t const p = readPtr(level, off);
        if (p < 0x10000 || inImage(p)) {
            continue;
        }
        for (std::uintptr_t map : {p, readPtr(p)}) {
            if (isBlockMap(map)) {
                log::info("block type map reached from Level +0x{:X}", off);
                return map;
            }
        }
    }
    return 0;
}

}

std::uintptr_t blockTypeMap(std::uintptr_t level) {
    if (std::uintptr_t m = mapViaMember(level)) {
        return m;
    }
    if (std::uintptr_t m = mapViaGetter(level)) {
        return m;
    }
    if (std::uintptr_t m = mapViaStatic()) {
        return m;
    }
    return r().has(n::BlockLookupMap) || r().slot(n::LevelGetBlockTypeRegistry) >= 0 || r().offset(n::LevelBlockTypeRegistry) >= 0 ? 0 : mapByContent(level);
}

int forEachBlockType(std::uintptr_t map, std::function<void(std::string const&, std::uintptr_t)> const& visit) {
    bool const         hashed  = hashedKeys();
    std::int64_t const mapped  = mappedAt();
    int                visited = 0;
    memory::forEachTreeNode(map, [&](std::uintptr_t node) {
        std::uintptr_t const key  = node + memory::TreeValue;
        std::string          name = hashed ? memory::readHashedString(key) : memory::readString(key);
        // the std::string-keyed map of older builds holds bare names ("gravel")
        if (!name.empty() && name.find(':') == std::string::npos) {
            name = "minecraft:" + name;
        }
        // The mapped value is usually an owner whose first qword is the heap BlockType; 1.26.50 stores the
        // BlockType* itself, whose first qword is then its vtable, in the image.
        std::uintptr_t owner = readPtr(node, mapped);
        std::uintptr_t bt    = owner ? readPtr(owner) : 0;
        if (bt && inImage(bt)) {
            bt = owner;
        }
        if (bt && !name.empty()) {
            visit(name, bt);
            ++visited;
        }
    });
    return visited;
}

std::uintptr_t dropStrategy(std::uintptr_t blockType) {
    if (std::int64_t const bound = r().offset(n::BlockTypeDropStrategy); bound >= 0) {
        return readPtr(blockType, bound);
    }
    std::int64_t const off = r().offset(n::BlockTypePermutations);
    return off < 0 ? 0 : readPtr(blockType, off + 0x88);
}

std::vector<std::uintptr_t> permutations(std::uintptr_t blockType) {
    std::vector<std::uintptr_t> out;
    std::int64_t                off = r().offset(n::BlockTypePermutations);
    if (off < 0) {
        return out;
    }
    std::uintptr_t begin = readPtr(blockType, off), end = readPtr(blockType, off + 8);
    if (!begin || end <= begin || (end - begin) / 8 > 65536) {
        return out;
    }
    for (std::uintptr_t at = begin; at < end; at += 8) {
        if (std::uintptr_t blk = readPtr(at)) {
            out.push_back(blk);
        }
    }
    return out;
}

std::uintptr_t defaultState(std::uintptr_t blockType) {
    std::int64_t off = r().offset(n::BlockTypePermutations);
    return off < 0 ? 0 : readPtr(blockType, off + 24);
}

}
