#include "memory/Image.h"

#ifdef _WIN32
#include <windows.h>

#include <cstring>
#else
#include "memory/Elf.h"
#include "memory/Mem.h"

#include <elf.h>
#include <link.h>

#include <charconv>
#include <cstdio>
#include <cstring>
#include <string_view>
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

std::vector<std::pair<std::uintptr_t, std::uintptr_t>> constantSections() { return readOnlySections(); }

std::vector<std::pair<std::uintptr_t, std::uintptr_t>> readWriteRegions() {
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> out;
    MEMORY_BASIC_INFORMATION                               region;
    for (std::uintptr_t at = 0x10000; VirtualQuery(reinterpret_cast<void const*>(at), &region, sizeof region) == sizeof region;) {
        std::uintptr_t const from = reinterpret_cast<std::uintptr_t>(region.BaseAddress), to = from + region.RegionSize;
        if (region.State == MEM_COMMIT && region.Type == MEM_PRIVATE && region.Protect == PAGE_READWRITE) {
            out.emplace_back(from, to);
        }
        if (to <= at) {
            break;
        }
        at = to;
    }
    return out;
}

namespace {

// The entry a chained one (a part of a function its compiler split off) is part of: where its unwind data says
// UNW_FLAG_CHAININFO, the entry after its unwind codes, until one that says nothing more
PRUNTIME_FUNCTION primaryOf(PRUNTIME_FUNCTION entry, DWORD64 base) {
    for (int i = 0; i < 32; ++i) {
        if (entry->UnwindData & 1) {
            entry = reinterpret_cast<PRUNTIME_FUNCTION>(base + (entry->UnwindData & ~1u));
            continue;
        }
        auto const* info = reinterpret_cast<std::uint8_t const*>(base + entry->UnwindData);
        if (!((info[0] >> 3) & UNW_FLAG_CHAININFO)) {
            break;
        }
        entry = reinterpret_cast<PRUNTIME_FUNCTION>(const_cast<std::uint8_t*>(info + 4 + 2 * ((info[2] + 1) & ~1)));
    }
    return entry;
}

}

std::pair<std::uintptr_t, std::uintptr_t> functionAround(std::uintptr_t pc) {
    DWORD64    base  = 0;
    auto const entry = RtlLookupFunctionEntry(pc, &base, nullptr);
    if (!entry) {
        return {0, 0};
    }
    // from the function's own start, where the parts from it to pc's follow each other
    auto const     primary = primaryOf(entry, base);
    std::uintptr_t end     = base + primary->EndAddress;
    while (end < base + entry->EndAddress) {
        DWORD64    nextBase = 0;
        auto const next     = RtlLookupFunctionEntry(end, &nextBase, nullptr);
        if (!next || primaryOf(next, base) != primary) {
            return {base + entry->BeginAddress, base + entry->EndAddress};
        }
        end = base + next->EndAddress;
    }
    return {base + primary->BeginAddress, base + entry->EndAddress};
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

std::vector<std::pair<std::uintptr_t, std::uintptr_t>> constantSections() {
    static std::vector<std::pair<std::uintptr_t, std::uintptr_t>> const out = [] {
        std::vector<std::pair<std::uintptr_t, std::uintptr_t>> all = segments().readOnly;
        ElfFile                                                exe;
        if (exe.open("/proc/self/exe")) {
            if (auto const rodata = exe.section(".rodata")) {
                std::uintptr_t const from = segments().base + rodata->first, to = from + rodata->second;
                if (from >= segments().code.first && to <= segments().code.second) {
                    all.emplace_back(from, to);
                }
            }
        }
        return all;
    }();
    return out;
}

std::vector<std::pair<std::uintptr_t, std::uintptr_t>> readWriteRegions() {
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> out;
    FILE*                                                  maps = std::fopen("/proc/self/maps", "r");
    if (!maps) {
        return out;
    }
    // "from-to perms offset dev inode [path]", the addresses in hex (read without sscanf: Version.h)
    char line[512];
    while (std::fgets(line, sizeof line, maps)) {
        std::string_view const text(line, std::strcspn(line, "\n"));
        char const* const      end  = text.data() + text.size();
        std::uintptr_t         from = 0, to = 0;
        auto const             dash = std::from_chars(text.data(), end, from, 16);
        if (dash.ec != std::errc{} || dash.ptr == end || *dash.ptr != '-') {
            continue;
        }
        auto const range = std::from_chars(dash.ptr + 1, end, to, 16);
        if (range.ec != std::errc{}) {
            continue;
        }
        std::string_view const rest  = text.substr(static_cast<std::size_t>(range.ptr - text.data()));
        std::size_t const      slash = rest.find_first_of("/[");
        std::string_view const path  = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash);
        if (rest.starts_with(" rw-p ") && (path.empty() || path == "[heap]")) {
            out.emplace_back(from, to);
        }
    }
    std::fclose(maps);
    return out;
}

namespace {

// .eh_frame_hdr: version 1, then how its eh_frame pointer, its count and its table are encoded, the table sorted
// (initial location, FDE) pairs relative to the header (DW_EH_PE_datarel | DW_EH_PE_sdata4, as compilers write it)
std::uintptr_t ehFrameHeader() {
    static std::uintptr_t const header = [] {
        std::uintptr_t found = 0;
        dl_iterate_phdr(
            [](dl_phdr_info* info, std::size_t, void* p) {
                for (int i = 0; i < info->dlpi_phnum; ++i) {
                    if (info->dlpi_phdr[i].p_type == PT_GNU_EH_FRAME) {
                        *static_cast<std::uintptr_t*>(p) = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
                    }
                }
                return 1;
            },
            &found);
        return found;
    }();
    return header;
}

}

std::pair<std::uintptr_t, std::uintptr_t> functionAround(std::uintptr_t pc) {
    std::uintptr_t const header = ehFrameHeader();
    std::uint8_t         head[12];
    if (!header || !safeRead(reinterpret_cast<void const*>(header), head, sizeof head) || head[0] != 1 || head[2] != 0x03 || head[3] != 0x3B) {
        return {0, 0};
    }
    std::uint32_t count;
    std::memcpy(&count, head + 8, 4);
    auto const* table = reinterpret_cast<std::int32_t const*>(header + 12);
    // the last entry whose initial location is at or before pc
    std::uint32_t low = 0, high = count;
    while (high - low > 1) {
        std::uint32_t const mid = (low + high) / 2;
        if (header + static_cast<std::intptr_t>(table[2 * mid]) <= pc) {
            low = mid;
        } else {
            high = mid;
        }
    }
    if (!count || header + static_cast<std::intptr_t>(table[2 * low]) > pc) {
        return {0, 0};
    }
    std::uintptr_t const begin = header + static_cast<std::intptr_t>(table[2 * low]), fde = header + static_cast<std::intptr_t>(table[2 * low + 1]);
    // the FDE: its length, its CIE's, its initial location (pcrel sdata4), then its address range (udata4)
    std::uint32_t range = 0;
    if (!safeRead(reinterpret_cast<void const*>(fde + 12), &range, 4) || pc >= begin + range) {
        return {0, 0};
    }
    return {begin, begin + range};
}

#endif

}
