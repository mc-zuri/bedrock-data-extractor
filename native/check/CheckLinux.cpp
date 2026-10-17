// bdg_check <bedrock_server> <build>   resolve every binding for <build> from the server's files on disk: its symbols
// (bedrock_server_symbols.debug next to it, else its own .dynsym), its code (and the patterns in it), its vtables as the
// loader would relocate them. Prints "name<TAB>value" lines; exit code 1 when a binding does not resolve. No server is
// started; what a build without symbols finds by content (game/Layout.cpp) is found only in the running server.
#include "bindings/Bindings.h"
#include "memory/Elf.h"

#include <cstdio>
#include <optional>
#include <set>
#include <string>

namespace {

class FileSymbols : public bdg::bindings::Registry::Symbols {
public:
    bool open(std::string const& exe) { return mExe.open(exe) && mSymbols.open(bdg::memory::symbolsFileOf(exe)); }

    std::map<std::string, std::uint64_t> addresses(std::vector<std::string> const& mangled) const override {
        return mSymbols.symbols(std::set<std::string>(mangled.begin(), mangled.end()));
    }
    bool bytes(std::uint64_t rva, void* out, std::size_t n) const override { return mExe.bytes(rva, out, n); }
    // the code as a Module for the patterns: the executable segment's bytes, at their virtual addresses
    [[nodiscard]] std::optional<bdg::memory::Module> code() const {
        auto const segment = mExe.codeSegment();
        if (!segment) {
            return std::nullopt;
        }
        auto const begin = reinterpret_cast<std::uintptr_t>(segment->data);
        return bdg::memory::Module{begin, begin + segment->size, static_cast<std::uint32_t>(segment->vaddr)};
    }
    std::uint64_t word(std::uint64_t rva) const override { return mExe.word(rva); }

private:
    bdg::memory::ElfFile mExe, mSymbols;
};

}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: bdg_check <bedrock_server> <build>\n");
        return 2;
    }
    FileSymbols files;
    if (!files.open(argv[1])) {
        std::fprintf(stderr, "cannot read %s or its symbols\n", argv[1]);
        return 2;
    }
    auto& r      = bdg::bindings::load(bdg::Version::parse(argv[2]));
    auto  failed = r.resolveNames(files);
    auto  code   = files.code();
    if (!code) {
        failed.push_back("no executable segment");
    } else {
        for (auto const& f : r.resolve(*code)) {
            failed.push_back(f);
        }
    }
    for (auto const& [name, value] : r.describe()) {
        std::printf("%s\t%s\n", name.c_str(), value.c_str());
    }
    for (auto const& f : failed) {
        std::printf("FAILED\t%s\n", f.c_str());
    }
    return failed.empty() ? 0 : 1;
}
