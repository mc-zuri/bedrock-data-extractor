#pragma once
// Fault-safe reads for walking live game objects by offset. Nothing here throws or faults: a bad address yields 0 / an
// empty string. On Windows a read runs under SEH; on Linux under a SIGSEGV / SIGBUS handler (memory/Guard.h).

#ifdef _WIN32
#include <windows.h>
#endif

#include "memory/Image.h"
#include "memory/Stl.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace bdg::memory {

#ifdef _WIN32
inline bool safeRead(void const* addr, void* out, std::size_t len) {
    __try {
        std::memcpy(out, addr, len);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
#else
bool                 safeRead(void const* addr, void* out, std::size_t len);
#endif

template <class T>
T read(std::uintptr_t base, std::int64_t off = 0) {
    T v{};
    if (base && off >= 0) {
        safeRead(reinterpret_cast<void const*>(base + off), &v, sizeof(T));
    }
    return v;
}

inline std::uintptr_t readPtr(std::uintptr_t base, std::int64_t off = 0) { return read<std::uintptr_t>(base, off); }

// the slots a virtual destructor takes: MSVC's one (scalar deleting), the Itanium ABI's two (complete, deleting); a
// virtual declared after it is that many slots further
#ifdef _WIN32
inline constexpr int DestructorSlots = 1;
#else
inline constexpr int DestructorSlots = 2;
#endif

// the function at `obj`'s vtable slot, 0 when anything along the way looks wrong
inline std::uintptr_t virtualAt(std::uintptr_t obj, std::int64_t slot) {
    if (obj < 0x10000 || slot < 0) {
        return 0;
    }
    std::uintptr_t vt = readPtr(obj);
    if (vt < 0x10000) {
        return 0;
    }
    std::uintptr_t fn = readPtr(vt, slot * 8);
    return fn < 0x10000 ? 0 : fn;
}

// The std::string at `at`, in the server's standard library (memory/Stl.h); "" when it is not one.
inline std::string readString(std::uintptr_t at) {
    std::uint8_t raw[32]{};
    if (!safeRead(reinterpret_cast<void const*>(at), raw, stringSize())) {
        return {};
    }
    auto const [data, size] = stringData(at, raw);
    std::string out(size, '\0');
    if (size == 0 || data < 0x10000 || !safeRead(reinterpret_cast<void const*>(data), out.data(), size)) {
        return {};
    }
    return out;
}

// The std::map / std::set at `map`: its root (0 when empty or unreadable), its element count, whether `node` is one.
inline std::uintptr_t treeRoot(std::uintptr_t map) {
    auto const     t    = treeLayout();
    std::uintptr_t root = t.headIsNil ? readPtr(readPtr(map), 8) : readPtr(map, t.rootAt);
    return root == (t.headIsNil ? readPtr(map) : 0) ? 0 : root;
}
inline std::uint64_t treeSize(std::uintptr_t map) { return readPtr(map, treeLayout().sizeAt); }

// Each node of the tree at `map`, in order (its value at node + TreeValue); a broken tree ends the walk, never hangs it.
template <class Visit>
int forEachTreeNode(std::uintptr_t map, Visit&& visit) {
    auto const           t    = treeLayout();
    std::uintptr_t const nil  = t.headIsNil ? readPtr(map) : 0;
    std::uintptr_t       node = treeRoot(map), stack[256];
    int                  sp = 0, visited = 0;
    for (int guard = 0; guard < 100000; ++guard) {
        while (node && node != nil && sp < 256) {
            stack[sp++] = node;
            node        = readPtr(node, t.left);
        }
        if (sp == 0) {
            break;
        }
        node = stack[--sp];
        visit(node);
        ++visited;
        node = readPtr(node, t.right);
    }
    return visited;
}

// HashedString: { uint64 hash; std::string str; HashedString const* lastMatch }
inline std::string readHashedString(std::uintptr_t at) { return readString(at + 8); }

inline std::size_t hashedStringSize() { return 8 + stringSize() + 8; }

}
