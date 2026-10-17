#pragma once
// The server's standard library, whose containers the agent walks by their layout: MSVC's on Windows (the clang-built
// servers from 1.26.20 use it too), libstdc++ on Linux up to the builds that switched, libc++ after (bindings set
// it: n::StlKind). Each reader takes the raw bytes it reads and says nothing about memory it did not.

#include <cstdint>
#include <cstring>
#include <utility>

namespace bdg::memory {

enum class Stl { Msvc, Libstdcxx, Libcxx };

inline Stl& stlKind() {
    static Stl kind =
#ifdef _WIN32
        Stl::Msvc;
#else
        Stl::Libcxx;
#endif
    return kind;
}

inline std::size_t stringSize() { return stlKind() == Stl::Libcxx ? 24 : 32; }

// A std::string's characters and size from its first stringSize() bytes, `raw` read at `at`; size 0 when it is none.
//   MSVC:      { union { char buf[16]; char* ptr; }; size_t size; size_t capacity }, the buffer used below 16
//   libstdc++: { char* ptr; size_t size; union { char buf[16]; size_t capacity } }, ptr at buf when short
//   libc++:    long { size_t cap | 1; size_t size; char* ptr }, short { uint8 size << 1; char buf[23] }
inline std::pair<std::uintptr_t, std::size_t> stringData(std::uintptr_t at, std::uint8_t const* raw) {
    std::uint64_t w0, w1, w2;
    std::memcpy(&w0, raw, 8);
    std::memcpy(&w1, raw + 8, 8);
    std::memcpy(&w2, raw + 16, 8);
    std::size_t    size = 0;
    std::uintptr_t data = 0;
    switch (stlKind()) {
    case Stl::Msvc: {
        std::uint64_t cap;
        std::memcpy(&cap, raw + 24, 8);
        size = w2;
        data = cap >= 16 ? w0 : at;
        break;
    }
    case Stl::Libstdcxx:
        size = w1;
        data = w0;
        break;
    case Stl::Libcxx:
        if (w0 & 1) {
            size = w1;
            data = w2;
        } else {
            size = raw[0] >> 1;
            data = at + 1;
        }
        break;
    }
    if (size > 4096) {
        return {0, 0};
    }
    return {data, size};
}

// std::map / std::set: a red-black tree. Its nodes hold the value at +32 in all three; what differs is where the
// tree hangs and how a leaf ends:
//   MSVC:      map { head*, size }; head { left = leftmost, parent = root, right = rightmost }; a node's children are
//              the head where it has none; node { left @0, parent @8, right @16, color @24, isnil @25 }
//   libstdc++: map { compare (8), header { color, parent = root @+16, left, right }, size @40 }; leaves null;
//              node { color @0, parent @8, left @16, right @24 }
//   libc++:    map { begin node*, end node { left = root } @8, size @16 }; leaves null;
//              node { left @0, right @8, parent @16, is_black @24 }
struct TreeLayout {
    std::int64_t rootAt, sizeAt, left, right;
    bool         headIsNil;
};

inline TreeLayout treeLayout() {
    switch (stlKind()) {
    case Stl::Msvc:
        return {0, 8, 0, 16, true};  // rootAt: the head pointer (root is head->parent)
    case Stl::Libstdcxx:
        return {16, 40, 16, 24, false};
    case Stl::Libcxx:
        return {8, 16, 0, 8, false};
    }
    return {};
}

constexpr std::int64_t TreeValue = 32;

}
