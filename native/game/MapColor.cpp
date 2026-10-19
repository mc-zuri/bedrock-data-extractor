#include "game/MapColor.h"

#include "core/Log.h"
#include "memory/Flow.h"
#include "memory/Guard.h"
#include "memory/Image.h"
#include "memory/Mem.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <functional>
#include <iterator>
#include <set>
#include <vector>

// Where Block::getMapColor is inline, each caller has, around its call of BlockLegacy::getMapColor:
//   component = a direct call taking the block, or (1.21.111) the same call taking the block's component storage
//               (block + 8), then where that answers null its BlockLegacy's ([block + legacy] + 0x28)
//               (null where neither has a map colour component)
//   if component: sampler = a direct call taking the byte at [component + tintAt]
//                 sample  = sampler's virtual at sampleSlot (BlockSource&, BlockPos const&)
//                 colour  = [component + colorAt], each component times the sample's
//   else:         colour  = [block + legacy]'s getMapColor (BlockSource&, BlockPos const&, Block const&)
// The parts are read from that code (decoded with Zydis): the load of [block + legacy] the call reads its vtable
// from, the branch to it on what the direct call answers of what it takes of the same block, and the other way from
// the branch.

namespace bdg::game {

using memory::gpr;
using memory::Instruction;

namespace {

// what a call that returns through a hidden pointer writes into: more than the colour's 16 bytes, should a wrong
// function write more there
struct alignas(16) ColorBuffer {
    Color4       color;
    std::uint8_t spare[0xF0];
};

constexpr int Rax = 0;
#ifdef _WIN32
constexpr int Arg0           = 1;  // rcx
constexpr int volatileRegs[] = {0, 1, 2, 8, 9, 10, 11};
#else
constexpr int Arg0           = 7;  // rdi
constexpr int volatileRegs[] = {0, 1, 2, 6, 7, 8, 9, 10, 11};
#endif

bool isVolatile(int reg) { return std::ranges::find(volatileRegs, reg) != std::end(volatileRegs); }

// whether `x` writes the general register (any of its widths): a call, the volatile ones
bool writes(Instruction const& x, int reg) {
    if (x.insn.mnemonic == ZYDIS_MNEMONIC_CALL && isVolatile(reg)) {
        return true;
    }
    for (int i = 0; i < x.insn.operand_count; ++i) {
        ZydisDecodedOperand const& op = x.ops[i];
        if (op.type == ZYDIS_OPERAND_TYPE_REGISTER && (op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) && gpr(op.reg.value) == reg) {
            return true;
        }
    }
    return false;
}

// [base + disp] (no index)
struct Address {
    int          base;
    std::int64_t disp;
};

std::optional<Address> addressOf(ZydisDecodedOperand const& op) {
    if (op.type != ZYDIS_OPERAND_TYPE_MEMORY || op.mem.index != ZYDIS_REGISTER_NONE || gpr(op.mem.base) < 0) {
        return std::nullopt;
    }
    return Address{gpr(op.mem.base), op.mem.disp.value};
}

// `mov reg64, [base + disp]`
struct Load {
    int     to;
    Address from;
};

std::optional<Load> loadOf(Instruction const& x) {
    if (x.insn.mnemonic != ZYDIS_MNEMONIC_MOV || x.ops[0].type != ZYDIS_OPERAND_TYPE_REGISTER || x.ops[0].size != 64) {
        return std::nullopt;
    }
    auto const from = addressOf(x.ops[1]);
    if (!from || gpr(x.ops[0].reg.value) < 0) {
        return std::nullopt;
    }
    return Load{gpr(x.ops[0].reg.value), *from};
}

// `mov reg64, reg64`: {to, from}
std::optional<std::pair<int, int>> copyOf(Instruction const& x) {
    if (x.insn.mnemonic != ZYDIS_MNEMONIC_MOV || x.ops[0].type != ZYDIS_OPERAND_TYPE_REGISTER || x.ops[1].type != ZYDIS_OPERAND_TYPE_REGISTER || x.ops[0].size != 64 ||
        x.ops[1].size != 64) {
        return std::nullopt;
    }
    int const to = gpr(x.ops[0].reg.value), from = gpr(x.ops[1].reg.value);
    if (to < 0 || from < 0) {
        return std::nullopt;
    }
    return std::pair{to, from};
}

// the target of a direct call or jump; 0 for others
std::uintptr_t targetOf(Instruction const& x) {
    ZyanU64 to = 0;
    if (x.ops[0].type != ZYDIS_OPERAND_TYPE_IMMEDIATE || !x.ops[0].imm.is_relative || !ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&x.insn, &x.ops[0], x.at, &to))) {
        return 0;
    }
    return static_cast<std::uintptr_t>(to);
}

bool isXmm(ZydisDecodedOperand const& op) { return op.type == ZYDIS_OPERAND_TYPE_REGISTER && op.reg.value >= ZYDIS_REGISTER_XMM0 && op.reg.value <= ZYDIS_REGISTER_XMM31; }

// whether registers `a` and `b` hold the same value before code[k], as the copies among the instructions before it say
bool same(std::vector<Instruction> const& code, std::size_t k, int a, int b) {
    std::array<int, 16> value;
    for (int r = 0; r < 16; ++r) {
        value[r] = r;
    }
    int next = 16;
    for (std::size_t i = k > 32 ? k - 32 : 0; i < k; ++i) {
        if (auto const copy = copyOf(code[i])) {
            value[copy->first] = value[copy->second];
            continue;
        }
        for (int r = 0; r < 16; ++r) {
            if (writes(code[i], r)) {
                value[r] = next++;
            }
        }
    }
    return value[a] == value[b];
}

// registers that hold one thing each, as the instructions copy, load and overwrite them
struct Holders {
    std::set<int> component, sampler, vtable;
    int           fn = -1;  // the sampler's virtual

    // `slot`, where given, takes the slot of a virtual loaded from a vtable
    void step(Instruction const& x, int* slot = nullptr) {
        std::set<int>* sets[] = {&component, &sampler, &vtable};
        if (x.insn.mnemonic == ZYDIS_MNEMONIC_CALL) {
            for (int reg : volatileRegs) {
                for (auto* s : sets) {
                    s->erase(reg);
                }
            }
            fn = -1;
            return;
        }
        if (auto const copy = copyOf(x)) {
            for (auto* s : sets) {
                if (s->contains(copy->second)) {
                    s->insert(copy->first);
                } else {
                    s->erase(copy->first);
                }
            }
            fn = fn == copy->second ? copy->first : fn == copy->first ? -1 : fn;
            return;
        }
        std::optional<Load> const load      = loadOf(x);
        bool const                toVtable  = load && sampler.contains(load->from.base) && load->from.disp == 0;
        bool const                toVirtual = load && vtable.contains(load->from.base) && load->from.disp >= 0 && load->from.disp % 8 == 0;
        for (int r = 0; r < 16; ++r) {
            if (writes(x, r)) {
                for (auto* s : sets) {
                    s->erase(r);
                }
                fn = fn == r ? -1 : fn;
            }
        }
        if (toVtable) {
            vtable.insert(load->to);
        }
        if (toVirtual && slot) {
            *slot = static_cast<int>(load->from.disp / 8);
            fn    = load->to;
        }
    }
};

// The other way from the branch, from code[from], where `holders.component` hold the component: a byte of it into the
// first argument and a direct call (the sampler), the sampler's virtual called, then the component's colour, read by SSE
// instructions before the next call or jump: the parts it reads
std::optional<MapColorParts> componentWay(std::vector<Instruction> const& code, std::size_t from, Holders holders) {
    MapColorParts p;
    enum class Step { Byte, Sampler, Sample, Color } step = Step::Byte;
    std::vector<std::pair<std::int64_t, int>> reads;  // the component's: offset, bytes
    for (std::size_t i = from; i < code.size() && i < from + 64; ++i) {
        Instruction const&  x    = code[i];
        ZydisMnemonic const m    = x.insn.mnemonic;
        bool const          call = m == ZYDIS_MNEMONIC_CALL;
        bool const          ends = m == ZYDIS_MNEMONIC_JMP || m == ZYDIS_MNEMONIC_RET || x.insn.meta.category == ZYDIS_CATEGORY_COND_BR;
        if (step == Step::Color && (call || ends)) {
            break;
        }
        if (ends) {
            return std::nullopt;
        }
        if (step == Step::Byte) {
            if (call) {
                return std::nullopt;
            }
            auto const byte = addressOf(x.ops[1]);
            if (m == ZYDIS_MNEMONIC_MOVZX && x.ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER && gpr(x.ops[0].reg.value) == Arg0 && byte && x.ops[1].size == 8 &&
                holders.component.contains(byte->base)) {
                p.tintAt = byte->disp;
                step     = Step::Sampler;
                holders.step(x);
                continue;
            }
        } else if (step == Step::Sampler) {
            if (call) {
                p.sampler = targetOf(x);
                if (!p.sampler) {
                    return std::nullopt;
                }
                holders.step(x);
                holders.sampler = {Rax};
                step            = Step::Sample;
                continue;
            }
            if (writes(x, Arg0)) {
                return std::nullopt;
            }
        } else if (step == Step::Sample && call) {
            ZydisDecodedOperand const& to      = x.ops[0];
            auto const                 through = addressOf(to);
            bool                       calls   = false;
            if (through && holders.vtable.contains(through->base) && through->disp >= 0 && through->disp % 8 == 0) {
                p.sampleSlot = static_cast<int>(through->disp / 8);
                calls        = true;
            } else if (to.type == ZYDIS_OPERAND_TYPE_MEMORY && to.mem.base == ZYDIS_REGISTER_RIP) {
                // Control Flow Guard's dispatch: the target in rax
                calls = holders.fn == Rax;
            } else if (to.type == ZYDIS_OPERAND_TYPE_REGISTER) {
                calls = holders.fn >= 0 && gpr(to.reg.value) == holders.fn;
            }
            if (!calls) {
                return std::nullopt;
            }
            holders.step(x);
            step = Step::Color;
            continue;
        } else if (step == Step::Color) {
            for (int o = 0; o < x.insn.operand_count_visible; ++o) {
                auto const at  = addressOf(x.ops[o]);
                bool const sse = std::any_of(x.ops, x.ops + x.insn.operand_count_visible, isXmm);
                if (at && sse && holders.component.contains(at->base)) {
                    reads.emplace_back(at->disp, x.ops[o].size / 8);
                }
            }
        }
        holders.step(x, step == Step::Sample ? &p.sampleSlot : nullptr);
    }
    if (step != Step::Color || reads.empty()) {
        return std::nullopt;
    }
    // the colour: 16 bytes of the component, all of them read
    p.colorAt = std::ranges::min(reads).first;
    std::array<bool, 16> covered{};
    for (auto const& [at, bytes] : reads) {
        for (std::int64_t b = at; b < at + bytes; ++b) {
            if (b - p.colorAt < 16) {
                covered[static_cast<std::size_t>(b - p.colorAt)] = true;
            }
        }
    }
    if (!std::ranges::all_of(covered, std::identity{})) {
        return std::nullopt;
    }
    return p;
}

// the index of the instruction at `at` in `code`; -1 when none starts there
std::ptrdiff_t indexAt(std::vector<Instruction> const& code, std::uintptr_t at) {
    auto const it = std::ranges::lower_bound(code, at, {}, &Instruction::at);
    return it != code.end() && it->at == at ? it - code.begin() : -1;
}

// The direct call whose answer the conditional jump code[j] tests (test r, r of rax or a copy of it, the copies right
// after the call): its index and the registers that hold its answer; nullopt for another jump
std::optional<std::pair<std::ptrdiff_t, std::set<int>>> testedCall(std::vector<Instruction> const& code, std::ptrdiff_t j) {
    if (j < 2) {
        return std::nullopt;
    }
    Instruction const& test = code[j - 1];
    if (test.insn.mnemonic != ZYDIS_MNEMONIC_TEST || test.ops[0].type != ZYDIS_OPERAND_TYPE_REGISTER || test.ops[1].type != ZYDIS_OPERAND_TYPE_REGISTER ||
        test.ops[0].reg.value != test.ops[1].reg.value) {
        return std::nullopt;
    }
    std::set<int>  answer{Rax};
    std::ptrdiff_t k = j - 2;
    for (; k >= 0 && k + 6 > j && code[k].insn.mnemonic != ZYDIS_MNEMONIC_CALL; --k) {
        auto const copy = copyOf(code[k]);
        if (!copy || copy->second != Rax) {
            return std::nullopt;
        }
        answer.insert(copy->first);
    }
    if (k < 0 || code[k].insn.mnemonic != ZYDIS_MNEMONIC_CALL || !targetOf(code[k]) || !answer.contains(gpr(test.ops[0].reg.value))) {
        return std::nullopt;
    }
    return std::pair{k, answer};
}

// What the direct call code[k] takes, where it is the block (in `block`) or its BlockLegacy, plus an offset: copied
// (mov), its address taken (lea), or [block + legacy] loaded and added to; with the index its instructions start at.
// Nullopt for anything else.
std::optional<std::pair<MapColorParts::Lookup, std::ptrdiff_t>> lookupAt(std::vector<Instruction> const& code, std::ptrdiff_t k, int block, std::int64_t legacy) {
    std::int64_t added = 0;
    for (std::ptrdiff_t i = k - 1; i >= 0 && i + 6 > k; --i) {
        Instruction const& x = code[i];
        if (!writes(x, Arg0)) {
            continue;
        }
        auto const load = loadOf(x);
        if (load && load->to == Arg0 && load->from.disp == legacy && same(code, static_cast<std::size_t>(i), load->from.base, block)) {
            return std::pair{MapColorParts::Lookup{true, added}, i};
        }
        if (added) {
            return std::nullopt;
        }
        if (x.insn.mnemonic == ZYDIS_MNEMONIC_ADD && x.ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER && x.ops[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
            added = x.ops[1].imm.value.s;
            continue;
        }
        auto const address = x.insn.mnemonic == ZYDIS_MNEMONIC_LEA ? addressOf(x.ops[1]) : std::nullopt;
        if (address && same(code, static_cast<std::size_t>(i), address->base, block)) {
            return std::pair{MapColorParts::Lookup{false, address->disp}, i};
        }
        auto const copy = copyOf(x);
        if (copy && same(code, static_cast<std::size_t>(i), copy->second, block)) {
            return std::pair{MapColorParts::Lookup{false, 0}, i};
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// The parts the place at code[h] (the load or call of [vtable + slot * 8]) calls, where it is an inline
// Block::getMapColor: the conditional jump to the legacy call (a few instructions of its arguments before the load)
// when the last lookup answers null, or on to it otherwise; that lookup, and a lookup before it whose answer jumps to
// the same component way
std::optional<MapColorParts> partsAt(std::vector<Instruction> const& code, std::size_t h, std::int64_t legacy, int slot) {
    // the call: [block + legacy], its vtable, the slot
    auto const call = addressOf(code[h].ops[code[h].insn.mnemonic == ZYDIS_MNEMONIC_MOV ? 1 : 0]);
    if (!call || call->disp != static_cast<std::int64_t>(slot) * 8) {
        return std::nullopt;
    }
    int            type = -1, block = -1;
    std::ptrdiff_t l = -1;
    for (std::size_t i = h; i-- > 0 && i + 8 > h;) {
        auto const load = loadOf(code[i]);
        if (load && type < 0 && load->to == call->base && load->from.disp == 0) {
            type = load->from.base;
        } else if (load && type >= 0 && load->to == type && load->from.disp == legacy) {
            block = load->from.base;
            l     = static_cast<std::ptrdiff_t>(i);
            break;
        }
    }
    if (l < 0 || isVolatile(block)) {
        return std::nullopt;
    }
    for (std::ptrdiff_t j = 2; j < l; ++j) {
        Instruction const& branch = code[j];
        bool const         ifNone = branch.insn.mnemonic == ZYDIS_MNEMONIC_JZ, ifSome = branch.insn.mnemonic == ZYDIS_MNEMONIC_JNZ;
        if (!ifNone && !ifSome) {
            continue;
        }
        std::ptrdiff_t const legacyFrom = ifNone ? indexAt(code, targetOf(branch)) : j + 1;
        std::ptrdiff_t const component  = ifNone ? j + 1 : indexAt(code, targetOf(branch));
        auto const           last       = testedCall(code, j);
        auto const           lookup     = last ? lookupAt(code, last->first, block, legacy) : std::nullopt;
        if (legacyFrom < 0 || component < 0 || legacyFrom > l || l - legacyFrom > 4 || !lookup) {
            continue;
        }
        MapColorParts parts;
        parts.component = targetOf(code[last->first]);
        parts.lookups   = {lookup->first};
        Holders holders;
        holders.component  = last->second;
        std::ptrdiff_t top = lookup->second;
        // a lookup before, whose answer jumps to the component way where it is not null
        std::ptrdiff_t const before = top - 1;
        if (before > 0 && code[before].insn.mnemonic == ZYDIS_MNEMONIC_JNZ && indexAt(code, targetOf(code[before])) == component) {
            auto const first       = testedCall(code, before);
            auto const firstLookup = first ? lookupAt(code, first->first, block, legacy) : std::nullopt;
            if (!firstLookup || targetOf(code[first->first]) != parts.component) {
                continue;
            }
            parts.lookups.insert(parts.lookups.begin(), firstLookup->first);
            std::erase_if(holders.component, [&](int r) { return !first->second.contains(r); });
            top = firstLookup->second;
        }
        // the block the legacy call reads, which nothing on the way to it changes
        bool kept = true;
        for (std::ptrdiff_t i = top; i < j; ++i) {
            kept = kept && !writes(code[i], block);
        }
        for (std::ptrdiff_t i = legacyFrom; i < l; ++i) {
            kept = kept && !writes(code[i], block);
        }
        if (!kept) {
            continue;
        }
        auto way = componentWay(code, static_cast<std::size_t>(component), holders);
        if (!way) {
            continue;
        }
        parts.tintAt     = way->tintAt;
        parts.sampler    = way->sampler;
        parts.sampleSlot = way->sampleSlot;
        parts.colorAt    = way->colorAt;
        parts.legacy     = legacy;
        parts.slot       = slot;
        return parts;
    }
    return std::nullopt;
}

// the displacement of a ModRM operand of the bytes at `at`; -1 for none (no base, or [rsp + ...])
std::int64_t displacement(std::uint8_t const* at, std::uint8_t modrm) {
    std::uint8_t const rm = modrm & 7, mod = modrm >> 6;
    if (rm == 4 || (mod != 1 && mod != 2)) {
        return -1;
    }
    std::int32_t disp = 0;
    if (mod == 2) {
        std::memcpy(&disp, at, 4);
    } else {
        disp = static_cast<std::int8_t>(at[0]);
    }
    return disp;
}

// The places in the image that load or call [reg + slot * 8] with a load of [reg + legacy] into a 64-bit register up to
// 0x40 bytes before
std::set<std::uintptr_t> slotCalls(std::int64_t legacy, int slot) {
    std::set<std::uintptr_t> out;
    auto const [from, to] = memory::imageCode();
    std::vector<std::uint8_t> code(1 << 20);
    std::int64_t const        wanted = static_cast<std::int64_t>(slot) * 8;
    auto                      loads  = [&](std::size_t i) { return (code[i] & 0xF8) == 0x48 && code[i + 1] == 0x8B; };
    for (std::uintptr_t at = from; at < to; at += code.size() - 0x80) {
        std::size_t const n = static_cast<std::size_t>(std::min<std::uintptr_t>(code.size(), to - at));
        if (!memory::safeRead(reinterpret_cast<void const*>(at), code.data(), n)) {
            continue;
        }
        for (std::size_t i = 0x40; i + 7 <= n; ++i) {
            bool const callsSlot = code[i] == 0xFF && (((code[i + 1] >> 3) & 7) == 2 || ((code[i + 1] >> 3) & 7) == 4) && displacement(&code[i + 2], code[i + 1]) == wanted;
            if (!callsSlot && !(loads(i) && displacement(&code[i + 3], code[i + 2]) == wanted)) {
                continue;
            }
            for (std::size_t j = i - 0x40; j < i; ++j) {
                if (loads(j) && displacement(&code[j + 3], code[j + 2]) == legacy) {
                    out.insert(at + i);
                    break;
                }
            }
        }
    }
    return out;
}

}

bool callMapColor(std::uintptr_t fn, std::uintptr_t state, std::uintptr_t region, void const* pos, Color4* out) {
    ColorBuffer buffer{};
    bool const  ok = memory::guard([&] {
#ifdef _WIN32
        reinterpret_cast<void* (*)(std::uintptr_t, ColorBuffer*, std::uintptr_t, void const*)>(fn)(state, &buffer, region, pos);
#else
        buffer.color = reinterpret_cast<Color4 (*)(std::uintptr_t, std::uintptr_t, void const*)>(fn)(state, region, pos);
#endif
    });
    *out = buffer.color;
    return ok;
}

std::optional<MapColorParts> inlinedMapColor(std::int64_t legacy, int slot, std::string& why) {
    std::vector<MapColorParts> found;
    std::size_t                places = 0;
    for (std::uintptr_t at : slotCalls(legacy, slot)) {
        auto const [begin, end] = memory::functionAround(at);
        if (!begin || end - begin > 0x20000) {
            continue;
        }
        ++places;
        auto const     code = memory::decode(begin, end, std::max(begin, at - 0x400), end);
        std::ptrdiff_t h    = indexAt(code, at);
        if (h < 0) {
            continue;
        }
        if (auto const parts = partsAt(code, static_cast<std::size_t>(h), legacy, slot)) {
            log::info("map colours: Block::getMapColor inline at rva 0x{:X}", at - memory::imageBase());
            found.push_back(*parts);
        }
    }
    if (found.empty()) {
        why = std::format("none of the {} places that call slot {} through Block::mLegacy at +{} has Block::getMapColor inline", places, slot, legacy);
        return std::nullopt;
    }
    if (!std::ranges::all_of(found, [&](MapColorParts const& p) { return p == found[0]; })) {
        why = std::format("the {} places that have Block::getMapColor inline call different parts", found.size());
        return std::nullopt;
    }
    MapColorParts const& p = found[0];
    std::string          lookups;
    for (auto const& [ofLegacy, offset] : p.lookups) {
        lookups += std::format("{} {} + {}", lookups.empty() ? "" : ", then", ofLegacy ? "its BlockLegacy" : "the block", offset);
    }
    log::info("map colours: inline in {} places: the component by rva 0x{:X} of{}, its tint method at +{} by rva 0x{:X}, the sample at slot {}, the colour at +{}", found.size(),
              p.component - memory::imageBase(), lookups, p.tintAt, p.sampler - memory::imageBase(), p.sampleSlot, p.colorAt);
    return p;
}

namespace {

// the component as the lookups ask it, each in turn while they answer null; false when a call faults
bool lookUp(MapColorParts const& parts, std::uintptr_t state, std::uintptr_t* component) {
    *component = 0;
    for (auto const& [ofLegacy, offset] : parts.lookups) {
        std::uintptr_t const of = (ofLegacy ? memory::readPtr(state, parts.legacy) : state) + offset;
        if (!memory::guard([&] { *component = reinterpret_cast<std::uintptr_t (*)(std::uintptr_t)>(parts.component)(of); })) {
            return false;
        }
        if (*component) {
            break;
        }
    }
    return true;
}

}

std::uintptr_t mapColorComponent(MapColorParts const& parts, std::uintptr_t state) {
    std::uintptr_t component = 0;
    return lookUp(parts, state, &component) ? component : 0;
}

bool askMapColor(MapColorParts const& parts, std::uintptr_t state, std::uintptr_t region, void const* pos, Color4* out) {
    ColorBuffer    buffer{};
    std::uintptr_t component = 0;
    if (!lookUp(parts, state, &component)) {
        return false;
    }
    if (!component) {
        std::uintptr_t const legacy = memory::readPtr(state, parts.legacy);
        std::uintptr_t const fn     = memory::virtualAt(legacy, parts.slot);
        bool const           ok     = fn && memory::guard([&] {
#ifdef _WIN32
                            reinterpret_cast<void* (*)(std::uintptr_t, ColorBuffer*, std::uintptr_t, void const*, std::uintptr_t)>(fn)(legacy, &buffer, region, pos, state);
#else
                            buffer.color = reinterpret_cast<Color4 (*)(std::uintptr_t, std::uintptr_t, void const*, std::uintptr_t)>(fn)(legacy, region, pos, state);
#endif
                        });
        *out = buffer.color;
        return ok;
    }
    std::uintptr_t sampler = 0;
    auto const     tint    = memory::read<std::uint8_t>(component, parts.tintAt);
    if (!memory::guard([&] { sampler = reinterpret_cast<std::uintptr_t (*)(int)>(parts.sampler)(tint); }) || !sampler) {
        return false;
    }
    std::uintptr_t const fn = memory::virtualAt(sampler, parts.sampleSlot);
    bool const           ok = fn && memory::guard([&] {
#ifdef _WIN32
                        reinterpret_cast<void* (*)(std::uintptr_t, ColorBuffer*, std::uintptr_t, void const*)>(fn)(sampler, &buffer, region, pos);
#else
                        buffer.color = reinterpret_cast<Color4 (*)(std::uintptr_t, std::uintptr_t, void const*)>(fn)(sampler, region, pos);
#endif
                    });
    for (int i = 0; i < 4; ++i) {
        out->c[i] = memory::read<float>(component, parts.colorAt + 4 * i) * buffer.color.c[i];
    }
    return ok;
}

}
