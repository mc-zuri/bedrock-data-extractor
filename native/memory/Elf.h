#pragma once
// A Linux server's ELF files, read from disk: bedrock_server (its code and data, by virtual address) and the symbols it
// has, its own .dynsym (1.16.201 - 1.16.210 export every symbol) or bedrock_server_symbols.debug's .symtab (1.16.220 -
// 1.21.2: every symbol, local ones too; the DWARF beside them covers only the bundled libraries, not the game). The
// server is read as shipped, never patched: what the agent resolves is the build's own.

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace bdg::memory {

class ElfFile {
public:
    // Maps `path` read-only; false when it is not a 64-bit x86 ELF file.
    bool open(std::string const& path);
    ~ElfFile();

    // The virtual address of each of `wanted` (mangled names) in .symtab, else .dynsym; absent where it has none.
    [[nodiscard]] std::map<std::string, std::uint64_t> symbols(std::set<std::string> const& wanted) const;

    // `n` bytes at `vaddr` as the file holds them (a loaded segment's content); false outside of one.
    bool bytes(std::uint64_t vaddr, void* out, std::size_t n) const;

    // The 8 bytes at `vaddr` as the loader leaves them: with their R_X86_64_RELATIVE relocation applied (a vtable's
    // entries, a pointer in .data.rel.ro), relative to a base of 0.
    [[nodiscard]] std::uint64_t word(std::uint64_t vaddr) const;

    // The name .gnu_debuglink gives the symbols file; "" when there is none.
    [[nodiscard]] std::string debugLink() const;

    // The executable segment as the file holds it (the patterns are matched in it)
    struct Bytes {
        std::uint8_t const* data;
        std::uint64_t       size, vaddr;
    };
    [[nodiscard]] std::optional<Bytes> codeSegment() const;

    // A section's virtual address and size, by its name; none where the file has no such section.
    [[nodiscard]] std::optional<std::pair<std::uint64_t, std::uint64_t>> section(std::string const& name) const;

private:
    struct Section {
        std::string   name;
        std::uint32_t type;
        std::uint64_t addr, offset, size, link, entsize;
    };
    struct Segment {
        std::uint64_t vaddr, offset, filesz;
        bool          executable;
    };
    std::uint8_t const* data(std::uint64_t offset, std::uint64_t size) const;
    void                indexRelocations() const;

    void*                                                        mMap  = nullptr;
    std::size_t                                                  mSize = 0;
    std::vector<Section>                                         mSections;
    std::vector<Segment>                                         mSegments;
    mutable std::vector<std::pair<std::uint64_t, std::uint64_t>> mRelative;  // offset -> addend, sorted
    mutable bool                                                 mRelocationsIndexed = false;
};

// The running server's symbols file: bedrock_server_symbols.debug next to the executable when there is one, else the
// executable itself (its .dynsym).
std::string symbolsFileOf(std::string const& exe);

}
