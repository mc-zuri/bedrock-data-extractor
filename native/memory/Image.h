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

std::pair<std::uintptr_t, std::uintptr_t> imageCode();

// the image's writable data, initialized or not
std::vector<std::pair<std::uintptr_t, std::uintptr_t>> dataSections();

// the image's read-only data (.rdata, .rodata: where the type names of its RTTI are, and its constant tables)
std::vector<std::pair<std::uintptr_t, std::uintptr_t>> readOnlySections();

// every place the image keeps constants (its string literals): readOnlySections, and on Linux an older build's .rodata,
// which its linker put in the executable segment
std::vector<std::pair<std::uintptr_t, std::uintptr_t>> constantSections();

// the process's own read-write memory, not the image's nor a file's: its heaps (Windows: committed private
// PAGE_READWRITE regions; Linux: the anonymous rw-p mappings and [heap])
std::vector<std::pair<std::uintptr_t, std::uintptr_t>> readWriteRegions();

// The function of the image `pc` is in: [begin, end), as its unwind data says (Windows: .pdata, from the start of the
// function a part split off from it is chained to, up to the end of pc's part; Linux: .eh_frame_hdr's table); {0, 0}
// when none says
std::pair<std::uintptr_t, std::uintptr_t> functionAround(std::uintptr_t pc);

}
