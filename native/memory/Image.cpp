#include "memory/Image.h"

#ifdef _WIN32
#include <windows.h>

#include <cstring>
#else
#include "memory/Elf.h"

#include <elf.h>
#include <link.h>

#include <cstdio>
#include <cstring>
#endif

namespace bdg::memory {

#ifdef _WIN32

namespace {

template <class Pick>
std::vector<std::pair<std::uintptr_t, std::uintptr_t>> sections(Pick pick) {
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> out;
    auto const                                             base = imageBase();
    auto const*                                            dos  = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    auto const*                                            nt   = reinterpret_cast<IMAGE_NT_HEADERS const*>(base + dos->e_lfanew);
    auto const*                                            sec  = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (pick(*sec)) {
            out.emplace_back(base + sec->VirtualAddress, base + sec->VirtualAddress + sec->Misc.VirtualSize);
        }
    }
    return out;
}

}

std::pair<std::uintptr_t, std::uintptr_t> imageCode() {
    auto const text = sections([](IMAGE_SECTION_HEADER const& s) { return std::memcmp(s.Name, ".text", 5) == 0; });
    return text.empty() ? std::pair<std::uintptr_t, std::uintptr_t>{0, 0} : text.front();
}

std::vector<std::pair<std::uintptr_t, std::uintptr_t>> dataSections() {
    return sections([](IMAGE_SECTION_HEADER const& s) { return (s.Characteristics & IMAGE_SCN_MEM_WRITE) && !(s.Characteristics & IMAGE_SCN_MEM_EXECUTE); });
}

std::vector<std::pair<std::uintptr_t, std::uintptr_t>> readOnlySections() {
    return sections(
        [](IMAGE_SECTION_HEADER const& s) { return (s.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) && !(s.Characteristics & (IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE)); });
}

#else

namespace {

// the executable's loaded segments: the first object dl_iterate_phdr reports is it
struct Segments {
    std::uintptr_t                                         base = 0, end = 0;
    std::pair<std::uintptr_t, std::uintptr_t>              code{0, 0};
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> data, readOnly;
};

Segments const& segments() {
    static Segments const s = [] {
        Segments out;
        dl_iterate_phdr(
            [](dl_phdr_info* info, std::size_t, void* p) {
                auto& o = *static_cast<Segments*>(p);
                o.base  = info->dlpi_addr;
                for (int i = 0; i < info->dlpi_phnum; ++i) {
                    ElfW(Phdr) const& ph = info->dlpi_phdr[i];
                    if (ph.p_type != PT_LOAD) {
                        continue;
                    }
                    std::uintptr_t const from = info->dlpi_addr + ph.p_vaddr, to = from + ph.p_memsz;
                    if (to > o.end) {
                        o.end = to;
                    }
                    if (ph.p_flags & PF_X) {
                        o.code = {from, to};
                    } else if (ph.p_flags & PF_W) {
                        o.data.emplace_back(from, to);
                    } else {
                        o.readOnly.emplace_back(from, to);
                    }
                }
                return 1;
            },
            &out);
        return out;
    }();
    return s;
}

}

ImageRange const& imageRange() {
    static ImageRange const range{segments().base, segments().end};
    return range;
}

std::pair<std::uintptr_t, std::uintptr_t> imageCode() { return segments().code; }

std::vector<std::pair<std::uintptr_t, std::uintptr_t>> dataSections() { return segments().data; }

std::vector<std::pair<std::uintptr_t, std::uintptr_t>> readOnlySections() { return segments().readOnly; }

#endif

}
