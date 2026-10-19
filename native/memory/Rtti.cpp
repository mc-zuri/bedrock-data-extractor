#include "memory/Rtti.h"

#include "memory/Image.h"
#include "memory/Mem.h"

#include <cstring>

namespace bdg::memory {

#ifdef _WIN32
// MSVC lays its RTTI out otherwise, and the Windows bindings name what Linux finds by it
std::uintptr_t vtableOf(std::string_view) { return 0; }

std::string classOf(std::uintptr_t) { return ""; }
#else
namespace {

// where in the image's data a word is `value`: its writable data, where a position independent build's typeinfos and
// vtables are (relocated), then where it keeps constants, where an older build's are (not position independent: 1.19.30's
// read-only data, 1.18.30's .rodata in its code segment)
template <class Visit>
void eachWordOf(std::uintptr_t value, Visit&& visit) {
    auto sections = dataSections();
    for (auto range : constantSections()) {
        sections.push_back(range);
    }
    for (auto const& [from, to] : sections) {
        for (std::uintptr_t at = (from + 7) & ~std::uintptr_t{7}; at + 8 <= to; at += 8) {
            if (*reinterpret_cast<std::uintptr_t const*>(at) == value && !visit(at)) {
                return;
            }
        }
    }
}

}

std::uintptr_t vtableOf(std::string_view typeName) {
    for (auto const& [from, to] : constantSections()) {
        auto const* end = reinterpret_cast<char const*>(to);
        for (char const* at = reinterpret_cast<char const*>(from); at + typeName.size() < end; ++at) {
            at = static_cast<char const*>(std::memchr(at, typeName[0], static_cast<std::size_t>(end - at)));
            if (!at || at + typeName.size() >= end) {
                break;
            }
            // the linker may have merged the name into the tail of a longer one (P11BlockSource): a typeinfo points at it
            if (std::memcmp(at, typeName.data(), typeName.size()) != 0 || at[typeName.size()] != 0) {
                continue;
            }
            std::uintptr_t vtable = 0;
            eachWordOf(reinterpret_cast<std::uintptr_t>(at), [&](std::uintptr_t namePointer) {
                std::uintptr_t const typeinfo = namePointer - 8;
                eachWordOf(typeinfo, [&](std::uintptr_t rttiSlot) {
                    if (readPtr(rttiSlot - 8) == 0) {
                        vtable = rttiSlot + 8;
                    }
                    return vtable == 0;
                });
                return vtable == 0;
            });
            if (vtable) {
                return vtable;
            }
        }
    }
    return 0;
}

std::string classOf(std::uintptr_t object) {
    std::uintptr_t const vtable = readPtr(object);
    if (!inImage(vtable)) {
        return "";
    }
    std::uintptr_t const typeinfo = readPtr(vtable - 8);
    if (!inImage(typeinfo)) {
        return "";
    }
    std::uintptr_t const name = readPtr(typeinfo, 8);
    if (!inImage(name)) {
        return "";
    }
    char text[256]{};
    for (std::size_t i = 0; i + 1 < sizeof text; ++i) {
        if (!safeRead(reinterpret_cast<void const*>(name + i), &text[i], 1)) {
            return "";
        }
        if (!text[i]) {
            return text;
        }
    }
    return "";
}
#endif

}
