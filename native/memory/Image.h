#pragma once
// The running server's image: where it is loaded, its code, and its writable data (where its statics are).

#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdint>
#include <utility>
#include <vector>

namespace bdg::memory {

#ifdef _WIN32
inline std::uintptr_t imageBase() {
    static std::uintptr_t const base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    return base;
}

// inside the loaded server image: a vtable or code pointer, as opposed to heap data
inline bool inImage(std::uintptr_t p) { return p >= imageBase() && p < imageBase() + 0x10000000; }
#else
// the executable's loaded extent, its lowest to its highest segment
struct ImageRange {
    std::uintptr_t base, end;
};
ImageRange const& imageRange();

inline std::uintptr_t imageBase() { return imageRange().base; }

inline bool inImage(std::uintptr_t p) { return p >= imageRange().base && p < imageRange().end; }
#endif

// the image's code: [begin, end)
std::pair<std::uintptr_t, std::uintptr_t> imageCode();

// the image's writable data, initialized or not
std::vector<std::pair<std::uintptr_t, std::uintptr_t>> dataSections();

// the image's read-only data (.rdata, .rodata: where the type names of its RTTI are, and its constant tables)
std::vector<std::pair<std::uintptr_t, std::uintptr_t>> readOnlySections();

}
