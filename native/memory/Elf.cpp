#include "memory/Elf.h"

#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace bdg::memory {

ElfFile::~ElfFile() {
    if (mMap) {
        munmap(mMap, mSize);
    }
}

bool ElfFile::open(std::string const& path) {
    int const fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    struct stat st {};
    if (fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(Elf64_Ehdr))) {
        ::close(fd);
        return false;
    }
    mSize = static_cast<std::size_t>(st.st_size);
    mMap  = mmap(nullptr, mSize, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mMap == MAP_FAILED) {
        mMap = nullptr;
        return false;
    }
    auto const* eh = reinterpret_cast<Elf64_Ehdr const*>(mMap);
    if (std::memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS64 || eh->e_machine != EM_X86_64) {
        return false;
    }
    for (int i = 0; i < eh->e_phnum; ++i) {
        auto const* ph = reinterpret_cast<Elf64_Phdr const*>(data(eh->e_phoff + i * sizeof(Elf64_Phdr), sizeof(Elf64_Phdr)));
        if (ph && ph->p_type == PT_LOAD) {
            mSegments.push_back({ph->p_vaddr, ph->p_offset, ph->p_filesz, (ph->p_flags & PF_X) != 0});
        }
    }
    auto const* shdrs = reinterpret_cast<Elf64_Shdr const*>(data(eh->e_shoff, std::uint64_t{eh->e_shnum} * sizeof(Elf64_Shdr)));
    if (!shdrs || eh->e_shstrndx >= eh->e_shnum) {
        return true;
    }
    Elf64_Shdr const& names = shdrs[eh->e_shstrndx];
    for (int i = 0; i < eh->e_shnum; ++i) {
        Elf64_Shdr const& s    = shdrs[i];
        char const*       name = reinterpret_cast<char const*>(data(names.sh_offset + s.sh_name, 1));
        mSections.push_back({name ? name : "", s.sh_type, s.sh_addr, s.sh_offset, s.sh_size, s.sh_link, s.sh_entsize});
    }
    return true;
}

std::uint8_t const* ElfFile::data(std::uint64_t offset, std::uint64_t size) const {
    if (!mMap || offset > mSize || size > mSize - offset) {
        return nullptr;
    }
    return static_cast<std::uint8_t const*>(mMap) + offset;
}

std::map<std::string, std::uint64_t> ElfFile::symbols(std::set<std::string> const& wanted) const {
    std::map<std::string, std::uint64_t> out;
    for (std::uint32_t const type : {std::uint32_t{SHT_SYMTAB}, std::uint32_t{SHT_DYNSYM}}) {
        for (Section const& s : mSections) {
            if (s.type != type || s.link >= mSections.size()) {
                continue;
            }
            Section const& strings = mSections[s.link];
            auto const*    syms    = reinterpret_cast<Elf64_Sym const*>(data(s.offset, s.size));
            if (!syms) {
                continue;
            }
            for (std::uint64_t i = 0, n = s.size / sizeof(Elf64_Sym); i < n; ++i) {
                Elf64_Sym const& sym = syms[i];
                if (!sym.st_value || sym.st_name >= strings.size) {
                    continue;
                }
                char const* name = reinterpret_cast<char const*>(data(strings.offset + sym.st_name, 1));
                if (!name) {
                    continue;
                }
                if (auto it = wanted.find(name); it != wanted.end()) {
                    out.emplace(*it, sym.st_value);
                }
            }
        }
        if (out.size() == wanted.size()) {
            break;
        }
    }
    return out;
}

bool ElfFile::bytes(std::uint64_t vaddr, void* out, std::size_t n) const {
    for (Segment const& seg : mSegments) {
        if (vaddr >= seg.vaddr && vaddr + n <= seg.vaddr + seg.filesz) {
            auto const* p = data(seg.offset + (vaddr - seg.vaddr), n);
            if (!p) {
                return false;
            }
            std::memcpy(out, p, n);
            return true;
        }
    }
    return false;
}

void ElfFile::indexRelocations() const {
    mRelocationsIndexed = true;
    for (Section const& s : mSections) {
        if (s.type != SHT_RELA) {
            continue;
        }
        auto const* rela = reinterpret_cast<Elf64_Rela const*>(data(s.offset, s.size));
        if (!rela) {
            continue;
        }
        for (std::uint64_t i = 0, n = s.size / sizeof(Elf64_Rela); i < n; ++i) {
            if (ELF64_R_TYPE(rela[i].r_info) == R_X86_64_RELATIVE) {
                mRelative.emplace_back(rela[i].r_offset, static_cast<std::uint64_t>(rela[i].r_addend));
            }
        }
    }
    std::ranges::sort(mRelative);
}

std::uint64_t ElfFile::word(std::uint64_t vaddr) const {
    if (!mRelocationsIndexed) {
        indexRelocations();
    }
    auto it = std::ranges::lower_bound(mRelative, std::pair<std::uint64_t, std::uint64_t>{vaddr, 0});
    if (it != mRelative.end() && it->first == vaddr) {
        return it->second;
    }
    std::uint64_t w = 0;
    return bytes(vaddr, &w, 8) ? w : 0;
}

std::string ElfFile::debugLink() const {
    for (Section const& s : mSections) {
        if (s.name == ".gnu_debuglink") {
            if (char const* name = reinterpret_cast<char const*>(data(s.offset, s.size))) {
                return std::string(name, strnlen(name, s.size));
            }
        }
    }
    return "";
}

std::optional<ElfFile::Bytes> ElfFile::codeSegment() const {
    for (Segment const& seg : mSegments) {
        if (seg.executable) {
            if (std::uint8_t const* p = data(seg.offset, seg.filesz)) {
                return Bytes{p, seg.filesz, seg.vaddr};
            }
        }
    }
    return std::nullopt;
}

std::optional<std::pair<std::uint64_t, std::uint64_t>> ElfFile::section(std::string const& name) const {
    for (Section const& s : mSections) {
        if (s.name == name && s.addr) {
            return std::pair{s.addr, s.size};
        }
    }
    return std::nullopt;
}

std::string symbolsFileOf(std::string const& exe) {
    std::filesystem::path const debug = std::filesystem::path(exe).parent_path() / "bedrock_server_symbols.debug";
    std::error_code             e;
    return std::filesystem::exists(debug, e) ? debug.string() : exe;
}

}
