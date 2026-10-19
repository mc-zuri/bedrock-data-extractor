#include "game/Enums.h"

#include "core/Log.h"
#include "memory/Code.h"
#include "memory/Flow.h"
#include "memory/Guard.h"
#include "memory/Image.h"
#include "memory/Mem.h"
#include "memory/Rtti.h"
#include "memory/Scanner.h"
#include "memory/Stl.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <tuple>

// The sound event, particle type and container type names are BidirectionalUnorderedMaps, static in the image's data:
// two std::unordered_maps, name -> enum and enum -> name, built at startup. Each is found among the lists of every
// unordered_map in the data (memory::hashLayout) whose nodes all read as a name and an id: the one holding names whose
// ids are known. The id -> name map is taken where it is found (a name may have an alias: 1.16.201's guardian.flop and
// flop), else the name -> id one. A name is a std::string, a particle's a HashedString from 1.21.0; a sound event or
// particle id an int, a container type's a signed byte.
//
// The note block instruments' sounds are a static std::vector<std::string> (to 1.21.42; the names "note."-prefixed from
// 1.19.60), and from 1.26.30 a constant table of { const char*, size } (or { size, const char* }): in the read-only data
// on Windows, in a relocated (writable until loaded) segment on Linux; 1.21.50 to 1.26.20 keep none, nor does Linux
// 1.26.30 (it builds the names in code).

namespace bdg::game {

using memory::readPtr;
using memory::readString;

namespace {

enum class Shape { NameToId, HashedNameToId, IdToName, IdToHashedName };

struct Found {
    std::vector<EnumName> entries;  // in the list's order
    Shape                 shape;
    int                   idBytes;
    bool                  byId() const { return shape == Shape::IdToName || shape == Shape::IdToHashedName; }
};

bool isName(std::string const& s) {
    return !s.empty() && s.size() <= 96 &&
           std::ranges::all_of(s, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == ':' || c == '-'; });
}

bool isNode(std::uintptr_t p) { return p >= 0x10000 && !(p & 7) && !memory::inImage(p); }

std::pair<std::uintptr_t, std::uintptr_t> listAt(std::uintptr_t at, memory::HashLayout const& layout) {
    if (!layout.throughHead) {
        return {0, readPtr(at)};
    }
    std::uintptr_t const head = readPtr(at);
    if (!isNode(head)) {
        return {0, 0};
    }
    return {head, readPtr(head)};
}

std::optional<EnumName> entryAt(std::uintptr_t value, Shape shape, int idBytes) {
    auto     idAt = [&](std::uintptr_t at) { return idBytes == 1 ? int{memory::read<std::int8_t>(at, 0)} : memory::read<std::int32_t>(at, 0); };
    EnumName e;
    switch (shape) {
    case Shape::NameToId:
        e.name = readString(value);
        e.id   = idAt(value + memory::stringSize());
        break;
    case Shape::HashedNameToId:
        e.name = memory::readHashedString(value);
        e.id   = idAt(value + memory::hashedStringSize());
        break;
    case Shape::IdToName:
        e.id   = idAt(value);
        e.name = readString(value + 8);
        break;
    case Shape::IdToHashedName:
        e.id   = idAt(value);
        e.name = memory::readHashedString(value + 8);
        break;
    }
    if (!isName(e.name) || e.id < -128 || e.id > 100000) {
        return std::nullopt;
    }
    return e;
}

// the list at `at`, every node read as `shape`; none where a node does not read or the list does not end
std::optional<std::vector<EnumName>> listOf(std::uintptr_t at, Shape shape, int idBytes) {
    auto const layout = memory::hashLayout();
    auto [head, node] = listAt(at, layout);
    if (!isNode(node) || node == head) {
        return std::nullopt;
    }
    if (!entryAt(node + layout.value, shape, idBytes)) {
        return std::nullopt;
    }
    std::vector<EnumName> out;
    for (int i = 0; isNode(node) && node != head && i < 20000; ++i, node = readPtr(node)) {
        auto e = entryAt(node + layout.value, shape, idBytes);
        if (!e) {
            return std::nullopt;
        }
        out.push_back(std::move(*e));
    }
    if (node != head || out.size() < 3) {
        return std::nullopt;
    }
    return out;
}

std::vector<Found> const& enumLists() {
    static std::vector<Found> const lists = [] {
        std::vector<Found> out;
        for (auto [from, to] : memory::dataSections()) {
            for (std::uintptr_t at = from; at + 8 <= to; at += 8) {
                if (!isNode(readPtr(at))) {
                    continue;
                }
                for (Shape shape : {Shape::NameToId, Shape::HashedNameToId, Shape::IdToName, Shape::IdToHashedName}) {
                    for (int idBytes : {4, 1}) {
                        if (auto entries = listOf(at, shape, idBytes)) {
                            out.push_back({std::move(*entries), shape, idBytes});
                        }
                    }
                }
            }
        }
        log::info("enums: {} name lists in the data", out.size());
        return out;
    }();
    return lists;
}

bool has(std::vector<EnumName> const& entries, std::string_view name, int id) {
    return std::ranges::any_of(entries, [&](EnumName const& e) { return e.name == name && e.id == id; });
}
bool named(std::vector<EnumName> const& entries, std::string_view name) {
    return std::ranges::any_of(entries, [&](EnumName const& e) { return e.name == name; });
}

EnumNames pick(int idBytes, std::function<bool(std::vector<EnumName> const&)> const& fits, char const* what) {
    Found const* best = nullptr;
    for (Found const& f : enumLists()) {
        if (f.idBytes == idBytes && fits(f.entries) && (!best || f.byId() > best->byId() || (f.byId() == best->byId() && f.entries.size() > best->entries.size()))) {
            best = &f;
        }
    }
    if (!best) {
        return {{}, std::format("no {} table found", what)};
    }
    std::vector<EnumName> entries = best->entries;
    std::ranges::sort(entries, [](EnumName const& x, EnumName const& y) { return std::tie(x.id, x.name) < std::tie(y.id, y.name); });
    // an alias gives an id two names, which only a name -> id map can say (from 1.21.60, the only particle map:
    // vaultconnection and windexplosion are both 90)
    if (best->byId() && std::ranges::adjacent_find(entries, {}, &EnumName::id) != entries.end()) {
        return {{}, std::format("the {} table names an id twice", what)};
    }
    log::info("enums: {} {} ({})", entries.size(), what, best->byId() ? "by id" : "by name");
    return {std::move(entries), ""};
}

std::string readChars(std::uintptr_t at, std::size_t size) {
    if (at < 0x10000 || size == 0 || size > 64) {
        return {};
    }
    std::string out(size, '\0');
    if (!memory::safeRead(reinterpret_cast<void const*>(at), out.data(), size)) {
        return {};
    }
    return out;
}

// a static std::vector<std::string> whose first two are `first` and `second`
std::vector<std::string> stringVectorStarting(std::string_view first, std::string_view second) {
    std::size_t const size = memory::stringSize();
    for (auto [from, to] : memory::dataSections()) {
        for (std::uintptr_t at = from; at + 24 <= to; at += 8) {
            std::uintptr_t const b = readPtr(at), e = readPtr(at, 8);
            if (!isNode(b) || e <= b || (e - b) % size || (e - b) / size > 64) {
                continue;
            }
            if (readString(b) != first || readString(b + size) != second) {
                continue;
            }
            std::vector<std::string> out;
            for (std::uintptr_t p = b; p < e; p += size) {
                out.push_back(readString(p));
            }
            return out;
        }
    }
    return {};
}

// a constant table of string views whose first two are `first` and `second`, until one that is no note's sound (the
// next table follows it)
std::vector<std::string> stringViewTableStarting(std::string_view first, std::string_view second) {
    auto sections = memory::readOnlySections();
    for (auto range : memory::dataSections()) {
        sections.push_back(range);
    }
    for (auto [from, to] : sections) {
        for (std::uintptr_t at = from; at + 32 <= to; at += 8) {
            for (bool sizeFirst : {false, true}) {
                auto view = [&](std::uintptr_t p) {
                    std::uintptr_t const a = readPtr(p), b = readPtr(p, 8);
                    return sizeFirst ? readChars(b, a) : readChars(a, b);
                };
                if (view(at) != first || view(at + 16) != second) {
                    continue;
                }
                std::vector<std::string> out;
                for (std::uintptr_t p = at; p + 16 <= to && out.size() < 64; p += 16) {
                    std::string name = view(p);
                    if (!isName(name) || !name.starts_with("note.")) {
                        break;
                    }
                    out.push_back(std::move(name));
                }
                return out;
            }
        }
    }
    return {};
}

}

EnumNames soundEvents() {
    return pick(
        4, [](auto const& e) { return has(e, "item.use.on", 0) && has(e, "hit", 1) && has(e, "step", 2); }, "sound events");
}

EnumNames particleTypes() {
    return pick(
        4, [](auto const& e) { return has(e, "bubble", 1) && named(e, "iconcrack") && named(e, "mobflame"); }, "particle types");
}

EnumNames containerTypes() {
    return pick(
        1, [](auto const& e) { return has(e, "INVENTORY", -1) && has(e, "CONTAINER", 0) && named(e, "MINECART_HOPPER"); }, "container types");
}

EnumNames containerNames() {
    // 1.16.201 names them otherwise: combined_hotbar_and_inventory_items, crafting_input_items, cursor_items
    auto const fits = [](auto const& e) {
        return (named(e, "hotbar_and_inventory") || named(e, "combined_hotbar_and_inventory_items")) && (named(e, "crafting_input") || named(e, "crafting_input_items")) &&
               (named(e, "cursor") || named(e, "cursor_items"));
    };
    EnumNames out = pick(1, fits, "container names");
    return out.error.empty() ? out : pick(4, fits, "container names");
}

EnumNames noteInstruments() {
    std::vector<std::string> names = stringVectorStarting("harp", "bd");
    if (names.empty()) {
        names = stringVectorStarting("note.harp", "note.bd");
    }
    if (names.empty()) {
        names = stringViewTableStarting("note.harp", "note.bd");
    }
    EnumNames out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (!isName(names[i])) {
            return {{}, std::format("instrument {} is no name", i)};
        }
        out.entries.push_back({static_cast<int>(i), names[i]});
    }
    log::info("enums: {} note block instruments", out.entries.size());
    return out;
}

// From 1.26.40 the game registers MapDecoration::Type with its reflection (entt meta, at startup): one block of code a
// type, in the enum's order, each taking its name's address (a string literal) and ending with the sentinel Count. The
// names are the literals the registering function takes the address of, from MarkerWhite's to Count's; a type's id is
// its place among them, which Count's (the number of types) confirms.
EnumNames mapDecorations() {
    EnumNames      out;
    std::uintptr_t first   = 0;
    char const     name[]  = "4D 61 72 6B 65 72 57 68 69 74 65 00";  // "MarkerWhite" and its terminating zero
    auto const     pattern = memory::Pattern::parse(name);
    for (auto const& [from, to] : memory::readOnlySections()) {
        if (auto const hits = pattern->find(from, to, 1); !hits.empty()) {
            first = hits[0];
        }
    }
    if (!first) {
        return out;
    }
    std::vector<memory::Reference> refs;
    for (memory::Reference const& r : memory::referencesTo(first)) {
        if (r.kind == memory::Ref::Lea) {
            refs.push_back(r);
        }
    }
    if (refs.size() != 1) {
        return out.error = std::format("{} instructions take MarkerWhite's address", refs.size()), out;
    }
    memory::Function const fn         = memory::functionAt(refs[0].at);
    auto const             targets    = memory::leaTargets(fn, true);
    auto                   at         = std::ranges::find(targets, first);
    auto const             isTypeName = [](std::string const& s) {
        return s.size() >= 3 && s.size() <= 40 && std::isupper(static_cast<unsigned char>(s[0])) &&
               std::ranges::all_of(s, [](char c) { return std::isalnum(static_cast<unsigned char>(c)); });
    };
    for (; at != targets.end(); ++at) {
        char buf[48]{};
        if (!memory::safeRead(reinterpret_cast<void const*>(*at), buf, sizeof buf - 1)) {
            continue;
        }
        std::string const s(buf, strnlen(buf, sizeof buf - 1));
        if (!isTypeName(s)) {
            continue;
        }
        if (s == "Count") {
            log::info("map decorations: {} types", out.entries.size());
            return out;
        }
        out.entries.push_back({static_cast<int>(out.entries.size()), s});
    }
    out.entries.clear();
    out.error = "the map decorations' registration has no Count";
    return out;
}

// ExplorationMapRegistry's array of ExplorationMapData: each entry a map type (an int), the decoration type it marks (a
// byte at +4) and the feature's name (a std::string at +8). 1.20.40 to 1.21.2 keep it as ExplorationMapRegistry::mRegistry,
// filled at startup: found as the entries of the image's data whose name is a known feature's, their stride the distance
// between neighbours. From 1.21.20 it is a function-local static of ExplorationMapRegistry::_getRegistry, built on its
// first call, which returns it: the one function that takes the address of "minecraft:village_snowy" or copies it (the
// features are namespaced from then on), called once. The array is every entry from the first on that reads a feature
// name. Before 1.20.40 the three exploration maps are code alone: their markers are read from it (codedMarkers). No
// entries and no error where the build has none of these.
namespace {

bool isFeature(std::string const& s) {
    return s.size() >= 3 && s.size() <= 40 && std::ranges::all_of(s, [](char c) { return (c >= 'a' && c <= 'z') || c == '_'; });
}

bool isNamespacedFeature(std::string const& s) { return s.starts_with("minecraft:") && isFeature(s.substr(10)); }

std::uintptr_t literalAt(std::string_view text) {
    std::string hex;
    for (char c : text) {
        hex += std::format("{:02X} ", static_cast<unsigned char>(c));
    }
    auto const pattern = memory::Pattern::parse(hex + "00");
    // a literal, not the tail of a longer one
    for (auto const& [from, to] : memory::constantSections()) {
        for (std::uintptr_t const at : pattern->find(from, to, 4096)) {
            if (at > from && memory::read<std::uint8_t>(at - 1, 0) == 0) {
                return at;
            }
        }
    }
    return 0;
}

std::uintptr_t builtRegistry() {
    std::uintptr_t const name = literalAt("minecraft:village_snowy");
    if (!name) {
        return 0;
    }
    std::vector<std::uintptr_t> refs;
    for (memory::Reference const& r : memory::referencesTo(name)) {
        if (r.kind == memory::Ref::Lea || r.kind == memory::Ref::Load) {
            refs.push_back(r.at);
        }
    }
    memory::Function const fn = refs.size() == 1 ? memory::functionAt(refs[0]) : memory::Function{};
    if (!fn) {
        log::info("exploration maps: {} instructions take or copy minecraft:village_snowy, in no function found", refs.size());
        return 0;
    }
    std::uintptr_t registry = 0;
    if (!memory::guard([&] { registry = reinterpret_cast<std::uintptr_t (*)()>(fn.begin)(); })) {
        log::info("exploration maps: ExplorationMapRegistry::_getRegistry at +0x{:X} faulted", fn.begin - memory::imageBase());
        return 0;
    }
    log::info("exploration maps: ExplorationMapRegistry::_getRegistry at +0x{:X} gave +0x{:X}, first named {}", fn.begin - memory::imageBase(), registry - memory::imageBase(),
              readString(registry + 8));
    return registry;
}

// the next entry: the first name past the first's (an entry names its structure type too, the same name, in a HashedString)
std::uintptr_t strideFrom(std::uintptr_t first) {
    std::string const name = readString(first + 8);
    for (std::uintptr_t d = 0x20; d <= 0x200; d += 8) {
        if (std::string const next = readString(first + d + 8); next != name && isNamespacedFeature(next)) {
            return d;
        }
    }
    return 0;
}

// Before 1.20.40 the exploration maps are code alone: the loot function asks the structure feature type of the map's
// destination (a function holding every feature's name, "mansion" among them), then, from a default marker, picks
// the map type and the marker type by it (memory::casesOfResult: a compare of the type, then a constant into the byte
// every case sets). A case's feature is named by asking that function each name the functions holding "mansion" hold (one
// may compare a short name as an immediate, which the other keeps as a literal), with the agent's std::string (the
// game's on Windows; on Linux where both use the same standard library). On Linux 1.19.21 to 1.19.80 no code reads a
// feature name's literal (a table of pointers holds them): there the calls of the loot function itself are tried
// (ExplorationMapFunction's virtuals, by its RTTI), and the names are those of the table; and the cases may be one
// lookup instead of compares (shiftedMarkers).
std::vector<std::string> featureNamesIn(std::set<std::uintptr_t> const& functions) {
    std::vector<std::string> out;
    for (std::uintptr_t const fn : functions) {
        for (std::uintptr_t const at : memory::dataTargets(memory::functionAt(fn))) {
            // a literal's start, not a piece of one copied a word at a time
            char buf[48]{};
            if (memory::read<std::uint8_t>(at - 1, 0) != 0 || !memory::safeRead(reinterpret_cast<void const*>(at), buf, sizeof buf - 1)) {
                continue;
            }
            if (std::string name(buf, strnlen(buf, sizeof buf - 1)); isFeature(name) && std::ranges::find(out, name) == out.end()) {
                out.push_back(std::move(name));
            }
        }
    }
    return out;
}

// the feature names of the table that holds a pointer to `literal` (8-byte entries around it that point at one)
std::vector<std::string> featureNamesBeside(std::uintptr_t literal) {
    std::vector<std::string> out;
    auto                     sections = memory::dataSections();
    for (auto range : memory::constantSections()) {
        sections.push_back(range);
    }
    for (auto [from, to] : sections) {
        for (std::uintptr_t at = from; at + 8 <= to; at += 8) {
            if (memory::read<std::uintptr_t>(at, 0) != literal) {
                continue;
            }
            for (std::uintptr_t p = std::max(from, at - 0x400); p + 8 <= std::min(to, at + 0x400); p += 8) {
                std::uintptr_t const name = memory::read<std::uintptr_t>(p, 0);
                char                 buf[48]{};
                if (name < 0x10000 || memory::read<std::uint8_t>(name - 1, 0) != 0 || !memory::safeRead(reinterpret_cast<void const*>(name), buf, sizeof buf - 1)) {
                    continue;
                }
                if (std::string text(buf, strnlen(buf, sizeof buf - 1)); isFeature(text) && std::ranges::find(out, text) == out.end()) {
                    out.push_back(std::move(text));
                }
            }
        }
    }
    return out;
}

// each direct call of the exploration map loot function's virtuals: the function called, and where (Linux, by its RTTI)
std::vector<std::pair<std::uintptr_t, std::uintptr_t>> lootFunctionCalls() {
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> out;
    std::uintptr_t const                                   vtable = memory::vtableOf("22ExplorationMapFunction");
    for (int slot = 0; vtable && slot < 8; ++slot) {
        std::uintptr_t const   fn = readPtr(vtable, slot * 8);
        memory::Function const f  = memory::functionAt(fn);
        if (!f || f.begin != fn) {
            continue;
        }
        for (memory::Instruction const& i : memory::decode(f.begin, f.end, f.begin, f.end)) {
            if (i.insn.mnemonic == ZYDIS_MNEMONIC_CALL && i.ops[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && i.ops[0].imm.is_relative) {
                out.emplace_back(i.at + i.insn.length + static_cast<std::uintptr_t>(i.ops[0].imm.value.s), i.at);
            }
        }
    }
    return out;
}

// The markers of a switch clang made a lookup of (Linux 1.19.20, 1.19.30): the type copied to a register by the
// instruction after the call (and maybe on to another), its low byte offset
// (`add bl, -4`) and checked (`cmp bl, 6`, `ja` to the default: `mov al, 12`; or `cmp bl, 7`, `jae`), then the marker that byte of a constant
// (`movabs rax, C`, `shr rax, cl`, cl the byte times 8). Each type with its marker, the default left out; none where the
// code after `callAt` is not this.
std::map<std::int64_t, std::int64_t> shiftedMarkers(std::uintptr_t callAt) {
    memory::Function const fn = memory::functionAt(callAt);
    if (!fn) {
        return {};
    }
    auto const code = memory::decode(fn.begin, fn.end, callAt, std::min(fn.end, callAt + 0x200));
    if (code.size() < 2 || code[0].insn.mnemonic != ZYDIS_MNEMONIC_CALL) {
        return {};
    }
    auto const     isReg = [](memory::Instruction const& i, int n) { return i.insn.operand_count_visible > n && i.ops[n].type == ZYDIS_OPERAND_TYPE_REGISTER; };
    auto const     isImm = [](memory::Instruction const& i, int n) { return i.insn.operand_count_visible > n && i.ops[n].type == ZYDIS_OPERAND_TYPE_IMMEDIATE; };
    std::set<int>  type;  // the registers holding it
    std::int64_t   offset = 0, last = -1, table = 0;
    std::uintptr_t defaultAt = 0;
    // the call's result copied by the next instruction: a later copy of eax is another call's
    if (code[1].insn.mnemonic == ZYDIS_MNEMONIC_MOV && isReg(code[1], 0) && isReg(code[1], 1) && code[1].ops[1].reg.value == ZYDIS_REGISTER_EAX) {
        type.insert(memory::gpr(code[1].ops[0].reg.value));
    }
    auto const holds = [&](memory::Instruction const& i) { return isReg(i, 0) && type.contains(memory::gpr(i.ops[0].reg.value)); };
    for (std::size_t k = 2; !type.empty() && k < code.size(); ++k) {
        memory::Instruction const& i    = code[k];
        auto const                 copy = i.insn.mnemonic == ZYDIS_MNEMONIC_MOV || i.insn.mnemonic == ZYDIS_MNEMONIC_MOVSX || i.insn.mnemonic == ZYDIS_MNEMONIC_MOVZX;
        if (copy && isReg(i, 0) && isReg(i, 1) && type.contains(memory::gpr(i.ops[1].reg.value))) {
            // copied on (1.19.20: `mov r14d, eax`, then `movsx ebx, r14b`)
            type.insert(memory::gpr(i.ops[0].reg.value));
        } else if (i.insn.mnemonic == ZYDIS_MNEMONIC_ADD && holds(i) && isImm(i, 1) && i.ops[0].size == 8) {
            offset = i.ops[1].imm.value.s;
        } else if (i.insn.mnemonic == ZYDIS_MNEMONIC_CMP && holds(i) && isImm(i, 1) && i.ops[0].size == 8) {
            // the next branch, a few moves on at most: `ja` past the last, or `jae` (1.19.80) at one past it
            for (std::size_t b = k + 1; b < std::min(code.size(), k + 4); ++b) {
                auto const jump = code[b].insn.mnemonic;
                if ((jump == ZYDIS_MNEMONIC_JNBE || jump == ZYDIS_MNEMONIC_JNB) && isImm(code[b], 0)) {
                    last      = i.ops[1].imm.value.s - (jump == ZYDIS_MNEMONIC_JNB ? 1 : 0);
                    defaultAt = code[b].at + code[b].insn.length + static_cast<std::uintptr_t>(code[b].ops[0].imm.value.s);
                }
                if (jump != ZYDIS_MNEMONIC_MOV) {
                    break;
                }
            }
        } else if (last >= 0 && i.insn.mnemonic == ZYDIS_MNEMONIC_MOV && isReg(i, 0) && isImm(i, 1) && i.ops[0].size == 64 && i.insn.length == 10) {
            table = i.ops[1].imm.value.s;
        } else if (table && i.insn.mnemonic == ZYDIS_MNEMONIC_SHR && isReg(i, 1) && i.ops[1].reg.value == ZYDIS_REGISTER_CL) {
            break;
        }
    }
    if (type.empty() || last < 2 || last > 7 || !table || !defaultAt) {
        return {};
    }
    // the default: the byte the way past the check moves first (1.19.80 moves the map type before it)
    auto const fallback = memory::decode(fn.begin, fn.end, defaultAt, std::min(fn.end, defaultAt + 0x20));
    auto const moved    = std::ranges::find_if(
           fallback | std::views::take(3), [&](memory::Instruction const& m) { return m.insn.mnemonic == ZYDIS_MNEMONIC_MOV && isReg(m, 0) && isImm(m, 1) && m.ops[0].size == 8; });
    if (moved == (fallback | std::views::take(3)).end()) {
        return {};
    }
    std::int64_t const                   byDefault = moved->ops[1].imm.value.u & 0xFF;
    std::map<std::int64_t, std::int64_t> out;
    for (std::int64_t k = 0; k <= last; ++k) {
        std::int64_t const marker = (static_cast<std::uint64_t>(table) >> (8 * k)) & 0xFF;
        if (marker != byDefault) {
            out.emplace(static_cast<std::int8_t>(k - offset), marker);
        }
    }
    return out;
}

std::map<std::int64_t, std::string> featureTypes(std::uintptr_t namer, std::vector<std::string> const& names) {
    std::map<std::int64_t, std::string> out;
    if (sizeof(std::string) != memory::stringSize()) {
        return out;
    }
    for (std::string const& name : names) {
        std::uint8_t type = 0;
        if (memory::guard([&] { type = reinterpret_cast<std::uint8_t (*)(std::string const&)>(namer)(name); })) {
            out.try_emplace(type, name);
        }
    }
    return out;
}

EnumNames codedMarkers() {
    std::uintptr_t const mansion = literalAt("mansion");
    if (!mansion) {
        return {};
    }
    std::set<std::uintptr_t> namers;
    for (memory::Reference const& r : memory::referencesTo(mansion)) {
        if (r.kind == memory::Ref::Lea || r.kind == memory::Ref::Load || r.kind == memory::Ref::Absolute) {
            if (memory::Function const fn = memory::functionAt(r.at)) {
                namers.insert(fn.begin);
                // Linux 1.17: the function holding the names maps a type to its name; the one after it, asking by
                // name, builds its own map from copies of them
                if (auto const next = memory::functionsIn(fn.end, fn.end + 0x40); !next.empty()) {
                    namers.insert(next.front().begin);
                }
            }
        }
    }
    // each call of a namer, then each call of the loot function (by its RTTI: only a lookup of the cases, whose code says
    // the function called is the namer, takes one of those)
    struct Call {
        std::uintptr_t namer, at;
        bool           byRtti;
    };
    std::vector<Call> calls;
    for (std::uintptr_t const namer : namers) {
        for (memory::Reference const& call : memory::referencesTo(namer)) {
            if (call.kind == memory::Ref::Call) {
                calls.push_back({namer, call.at, false});
            }
        }
    }
    for (auto const& [namer, at] : lootFunctionCalls()) {
        calls.push_back({namer, at, true});
    }
    // the names to ask the namer: those the namers hold, and those of the table beside the literal
    std::optional<std::vector<std::string>> names;
    auto const                              featureNames = [&] {
        if (!names) {
            names = featureNamesIn(namers);
            for (std::string& name : featureNamesBeside(mansion)) {
                if (std::ranges::find(*names, name) == names->end()) {
                    names->push_back(std::move(name));
                }
            }
        }
        return *names;
    };
    for (auto const& [namer, callAt, byRtti] : calls) {
        // the marker: the byte the cases set each to its own value (its last on each case's way)
        std::vector<memory::ResultCase> const cases    = memory::casesOfResult(callAt);
        auto const                            lastByte = [](memory::ResultCase const& c, std::int64_t place) -> std::optional<std::int64_t> {
            for (auto const& m : c.moved | std::views::reverse) {
                if (m.bits == 8 && m.location == place) {
                    return m.value;
                }
            }
            return std::nullopt;
        };
        std::set<std::int64_t> places;
        for (memory::ResultCase const& c : cases) {
            for (auto const& m : c.moved) {
                if (m.bits == 8) {
                    places.insert(m.location);
                }
            }
        }
        std::int64_t place  = 0;
        std::size_t  values = 0;
        for (std::int64_t const p : places) {
            std::set<std::int64_t> distinct;
            for (memory::ResultCase const& c : cases) {
                if (auto const v = lastByte(c, p)) {
                    distinct.insert(*v);
                }
            }
            if (distinct.size() > values) {
                std::tie(place, values) = std::pair{p, distinct.size()};
            }
        }
        if (values < 3 || byRtti) {
            // a lookup instead of compares
            std::map<std::int64_t, std::int64_t> const shifted = shiftedMarkers(callAt);
            if (shifted.size() < 3) {
                continue;
            }
            std::map<std::int64_t, std::string> const features = featureTypes(namer, featureNames());
            EnumNames                                 out;
            for (auto const& [type, marker] : shifted) {
                auto const named = features.find(type);
                out.entries.push_back({static_cast<int>(marker), named == features.end() ? "" : named->second});
            }
            log::info("exploration maps: {} markers in a lookup of the loot code at +0x{:X}{}, {} features named", out.entries.size(), callAt - memory::imageBase(),
                      byRtti ? " (by its RTTI)" : "", features.size());
            return out;
        }
        std::map<std::int64_t, std::string> const features = featureTypes(namer, featureNames());
        // a compare whose way keeps the default marker (clang compares the type again past the switch) is none
        std::optional<std::int64_t> const fallback = lastByte(cases.back(), place);
        EnumNames                         out;
        for (memory::ResultCase const& c : cases | std::views::take(cases.size() - 1)) {
            if (auto const marker = lastByte(c, place); marker && marker != fallback) {
                auto const named = features.find(c.compared);
                out.entries.push_back({static_cast<int>(*marker), named == features.end() ? "" : named->second});
            }
        }
        if (out.entries.size() < 3) {
            continue;
        }
        log::info("exploration maps: {} markers in the loot code at +0x{:X}, {} features named", out.entries.size(), callAt - memory::imageBase(), features.size());
        return out;
    }
    return {};
}

EnumNames markersFrom(std::uintptr_t first, std::uintptr_t stride, bool (*named)(std::string const&)) {
    EnumNames out;
    for (std::uintptr_t p = first; named(readString(p + 8)) && out.entries.size() < 64; p += stride) {
        out.entries.push_back({memory::read<std::uint8_t>(p, 4), readString(p + 8)});
    }
    log::info("exploration maps: {} markers, entries 0x{:X} apart", out.entries.size(), stride);
    return out;
}

}

EnumNames explorationMarkers() {
    constexpr std::string_view  Known[] = {"monument", "mansion", "buried_treasure"};
    std::vector<std::uintptr_t> at;
    for (auto [from, to] : memory::dataSections()) {
        for (std::uintptr_t p = from; p + 0x30 <= to; p += 8) {
            if (std::string const s = readString(p + 8); std::ranges::find(Known, s) != std::end(Known) && memory::read<std::uint8_t>(p, 4) < 64) {
                at.push_back(p);
            }
        }
    }
    if (at.size() < 2) {
        std::uintptr_t const registry = builtRegistry();
        if (!registry || !isNamespacedFeature(readString(registry + 8))) {
            log::info("exploration maps: no registry");
            return codedMarkers();
        }
        std::uintptr_t const stride = strideFrom(registry);
        if (!stride) {
            return {{}, "the exploration map registry holds one entry"};
        }
        return markersFrom(registry, stride, isNamespacedFeature);
    }
    std::ranges::sort(at);
    std::uintptr_t stride = 0;
    for (std::size_t i = 1; i < at.size(); ++i) {
        if (std::uintptr_t const d = at[i] - at[i - 1]; d && d <= 0x200 && (!stride || d < stride)) {
            stride = d;
        }
    }
    if (!stride) {
        return {{}, "the exploration map entries lie apart"};
    }
    std::uintptr_t first = at.front();
    while (isFeature(readString(first - stride + 8))) {
        first -= stride;
    }
    return markersFrom(first, stride, isFeature);
}

}
