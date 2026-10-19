#include "memory/Code.h"

#include "memory/Image.h"
#include "memory/Mem.h"

#include <Zydis/Zydis.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <elf.h>
#include <link.h>
#endif

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace bdg::memory {

namespace {

// where each function begins, in address order; `parent` (Windows): the entry a chained part belongs to, else itself
struct Entry {
    std::uintptr_t begin, end;
    std::size_t    parent;
};

struct Table {
    std::vector<Entry>       entries;    // every entry, by begin
    std::vector<std::size_t> primaries;  // the entries that are no part of another, by begin
};

#ifdef _WIN32
// The exception directory's RUNTIME_FUNCTIONs. A part's UNWIND_INFO flags UNW_FLAG_CHAININFO and holds, after its unwind
// codes (an even count of 2-byte slots), the RUNTIME_FUNCTION of the part it continues.
Table readTable() {
    Table             t;
    auto const        base = imageBase();
    auto const*       dos  = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    auto const*       nt   = reinterpret_cast<IMAGE_NT_HEADERS const*>(base + dos->e_lfanew);
    auto const&       dir  = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    auto const*       fns  = reinterpret_cast<RUNTIME_FUNCTION const*>(base + dir.VirtualAddress);
    std::size_t const n    = dir.Size / sizeof(RUNTIME_FUNCTION);
    for (std::size_t i = 0; i < n; ++i) {
        t.entries.push_back({base + fns[i].BeginAddress, base + fns[i].EndAddress, i});
    }
    // the table is sorted by begin, as the loader requires
    auto const indexOf = [&](std::uintptr_t begin) -> std::size_t {
        auto const it = std::lower_bound(t.entries.begin(), t.entries.end(), begin, [](Entry const& e, std::uintptr_t b) { return e.begin < b; });
        return it != t.entries.end() && it->begin == begin ? static_cast<std::size_t>(it - t.entries.begin()) : n;
    };
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t at = i;
        for (int depth = 0; depth < 8; ++depth) {
            auto const* info = reinterpret_cast<std::uint8_t const*>(base + (fns[at].UnwindData & ~1u));
            if (!(info[0] >> 3 & UNW_FLAG_CHAININFO)) {
                break;
            }
            auto const*       chained = reinterpret_cast<RUNTIME_FUNCTION const*>(info + 4 + ((info[2] + 1) & ~1) * 2);
            std::size_t const parent  = indexOf(base + chained->BeginAddress);
            if (parent == n) {
                break;
            }
            at = parent;
        }
        t.entries[i].parent = at;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (t.entries[i].parent == i) {
            t.primaries.push_back(i);
        }
    }
    return t;
}
#else
// .eh_frame_hdr: version, the encodings of eh_frame_ptr, fde_count and the table (pcrel sdata4, udata4 and datarel
// sdata4, as the toolchains write them), then a table of { the function's start, its FDE }, sorted by start, relative to
// the header. A function's end is where the next one begins.
Table readTable() {
    Table          t;
    std::uintptr_t hdr = 0;
    dl_iterate_phdr(
        [](dl_phdr_info* info, std::size_t, void* p) {
            for (int i = 0; i < info->dlpi_phnum; ++i) {
                if (info->dlpi_phdr[i].p_type == PT_GNU_EH_FRAME) {
                    *static_cast<std::uintptr_t*>(p) = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
                }
            }
            return 1;
        },
        &hdr);
    auto const* h = reinterpret_cast<std::uint8_t const*>(hdr);
    if (!hdr || h[0] != 1 || h[2] != 0x03 || h[3] != 0x3b) {
        return t;
    }
    std::uint32_t count;
    std::memcpy(&count, h + 8, 4);
    auto const* table = reinterpret_cast<std::int32_t const*>(h + 12);
    for (std::uint32_t i = 0; i < count; ++i) {
        t.entries.push_back({hdr + static_cast<std::intptr_t>(table[2 * i]), 0, i});
    }
    for (std::size_t i = 0; i < t.entries.size(); ++i) {
        t.entries[i].end = i + 1 < t.entries.size() ? t.entries[i + 1].begin : imageCode().second;
        t.primaries.push_back(i);
    }
    return t;
}
#endif

Table const& table() {
    static Table const t = readTable();
    return t;
}

Function primary(std::size_t index) {
    Table const& t = table();
    Entry const& e = t.entries[index];
    // up to the next primary's begin: a function's own chained parts may lie between
    auto const next = std::upper_bound(t.primaries.begin(), t.primaries.end(), e.begin, [&](std::uintptr_t b, std::size_t i) { return b < t.entries[i].begin; });
    return {e.begin, next == t.primaries.end() ? e.end : t.entries[*next].begin};
}

struct Decoded {
    ZydisDecodedInstruction insn;
    ZydisDecodedOperand     operands[ZYDIS_MAX_OPERAND_COUNT];
    std::uintptr_t          at;
    std::uintptr_t          next() const { return at + insn.length; }
};

class Decoder {
public:
    Decoder() { mOk = ZYAN_SUCCESS(ZydisDecoderInit(&mDecoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64)); }

    bool decode(std::uintptr_t at, Decoded& out) {
        std::uint8_t bytes[ZYDIS_MAX_INSTRUCTION_LENGTH];
        if (!mOk || !safeRead(reinterpret_cast<void const*>(at), bytes, sizeof bytes)) {
            return false;
        }
        out.at = at;
        return ZYAN_SUCCESS(ZydisDecoderDecodeFull(&mDecoder, bytes, sizeof bytes, &out.insn, out.operands));
    }

private:
    ZydisDecoder mDecoder{};
    bool         mOk = false;
};

// a rip-relative operand's or a relative immediate's target; 0 where the instruction has none
std::uintptr_t targetOf(Decoded const& d, int operand) {
    ZyanU64 to = 0;
    if (!ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&d.insn, &d.operands[operand], d.at, &to))) {
        return 0;
    }
    return static_cast<std::uintptr_t>(to);
}

bool isDirectBranch(Decoded const& d) {
    return (d.insn.mnemonic == ZYDIS_MNEMONIC_CALL || d.insn.mnemonic == ZYDIS_MNEMONIC_JMP) && d.operands[0].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && d.operands[0].imm.is_relative;
}

bool inCode(std::uintptr_t p) { return p >= imageCode().first && p < imageCode().second; }

bool isConstant(std::uintptr_t p) {
    return std::ranges::any_of(constantSections(), [p](auto const& r) { return p >= r.first && p < r.second; });
}

}

Function functionAt(std::uintptr_t address) {
    Table const& t  = table();
    auto const   it = std::upper_bound(t.entries.begin(), t.entries.end(), address, [](std::uintptr_t a, Entry const& e) { return a < e.begin; });
    if (it == t.entries.begin()) {
        return {};
    }
    Entry const& e = *(it - 1);
#ifdef _WIN32
    if (address >= e.end) {
        return {};
    }
#endif
    return primary(e.parent);
}

Function functionBefore(Function fn) {
    Table const& t  = table();
    auto const   it = std::lower_bound(t.primaries.begin(), t.primaries.end(), fn.begin, [&](std::size_t i, std::uintptr_t b) { return t.entries[i].begin < b; });
    if (it == t.primaries.begin()) {
        return {};
    }
    return primary(*(it - 1));
}

std::vector<Function> functionsIn(std::uintptr_t from, std::uintptr_t to) {
    Table const&          t = table();
    std::vector<Function> out;
    for (std::size_t i : t.primaries) {
        if (t.entries[i].begin >= from && t.entries[i].begin < to) {
            out.push_back(primary(i));
        }
    }
    return out;
}

std::vector<Reference> referencesTo(std::uintptr_t target) {
    std::vector<Reference> out;
    auto const [begin, end] = imageCode();
    auto const*       code  = reinterpret_cast<std::uint8_t const*>(begin);
    std::size_t const n     = end - begin;
    for (std::size_t i = 3; i + 4 <= n; ++i) {
        std::int32_t disp;
        std::memcpy(&disp, code + i, 4);
        // an image loaded below 2 GiB (Linux's older builds, not position independent) takes an address as an imm32
        if (target < 0x80000000 && static_cast<std::uintptr_t>(static_cast<std::uint32_t>(disp)) == target) {
            out.push_back({begin + i, Ref::Absolute});
        }
        if (begin + i + 4 + static_cast<std::intptr_t>(disp) != target) {
            continue;
        }
        if (code[i - 1] == 0xE8) {
            out.push_back({begin + i - 1, Ref::Call});
        } else if (code[i - 1] == 0xE9) {
            out.push_back({begin + i - 1, Ref::Jump});
        }
        // REX.W, 8D, ModRM mod 00 r/m 101: lea r64, [rip + disp32]
        else if (code[i - 2] == 0x8D && (code[i - 1] & 0xC7) == 0x05 && (code[i - 3] & 0xF8) == 0x48) {
            out.push_back({begin + i - 3, Ref::Lea});
        }
        // 8B (mov r32 or, with REX.W, r64), ModRM mod 00 r/m 101: a short literal copied a word at a time
        else if (code[i - 2] == 0x8B && (code[i - 1] & 0xC7) == 0x05) {
            out.push_back({begin + i - 2, Ref::Load});
        }
        // 0F 10 (movups), 0F 28 (movaps), F3 0F 6F (movdqu), ModRM mod 00 r/m 101: xmm, [rip + disp32]
        else if (code[i - 3] == 0x0F && (code[i - 2] == 0x10 || code[i - 2] == 0x28 || code[i - 2] == 0x6F) && (code[i - 1] & 0xC7) == 0x05) {
            out.push_back({begin + i - 3, Ref::Load});
        }
    }
    return out;
}

std::vector<std::uintptr_t> leaTargets(Function fn, bool data) {
    std::vector<std::uintptr_t> out;
    Decoder                     decoder;
    Decoded                     d;
    for (std::uintptr_t at = fn.begin; at < fn.end;) {
        if (!decoder.decode(at, d)) {
            ++at;
            continue;
        }
        if (d.insn.mnemonic == ZYDIS_MNEMONIC_LEA && d.operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY && d.operands[1].mem.base == ZYDIS_REGISTER_RIP) {
            if (std::uintptr_t const to = targetOf(d, 1); to && inCode(to) != data) {
                out.push_back(to);
            }
        }
        at = d.next();
    }
    return out;
}

std::vector<std::uintptr_t> dataTargets(Function fn) {
    std::vector<std::uintptr_t> out;
    Decoder                     decoder;
    Decoded                     d;
    for (std::uintptr_t at = fn.begin; at < fn.end;) {
        if (!decoder.decode(at, d)) {
            ++at;
            continue;
        }
        for (int i = 0; i < d.insn.operand_count_visible; ++i) {
            auto const& op = d.operands[i];
            if (op.type == ZYDIS_OPERAND_TYPE_MEMORY && op.mem.base == ZYDIS_REGISTER_RIP) {
                if (std::uintptr_t const to = targetOf(d, i); to && (!inCode(to) || isConstant(to))) {
                    out.push_back(to);
                }
            } else if (op.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && !op.imm.is_relative) {
                // an absolute address (an image below 2 GiB)
                if (auto const to = static_cast<std::uintptr_t>(op.imm.value.u); inImage(to) && (!inCode(to) || isConstant(to))) {
                    out.push_back(to);
                }
            }
        }
        at = d.next();
    }
    return out;
}

namespace {

// A register (by its 64-bit name) or a stack slot ([rsp|rbp + disp]); 0 for anything else.
std::int64_t locationOf(ZydisDecodedOperand const& op) {
    if (op.type == ZYDIS_OPERAND_TYPE_REGISTER) {
        return ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, op.reg.value);
    }
    if (op.type == ZYDIS_OPERAND_TYPE_MEMORY && op.mem.index == ZYDIS_REGISTER_NONE && (op.mem.base == ZYDIS_REGISTER_RSP || op.mem.base == ZYDIS_REGISTER_RBP)) {
        return (static_cast<std::int64_t>(op.mem.base) << 32) ^ (op.mem.disp.value + 0x10000);
    }
    return 0;
}

bool isMove(ZydisMnemonic m) { return m == ZYDIS_MNEMONIC_MOV || m == ZYDIS_MNEMONIC_MOVZX || m == ZYDIS_MNEMONIC_MOVSX; }

}

std::vector<ResultCase> casesOfResult(std::uintptr_t call, std::size_t limit) {
    std::vector<ResultCase> out;
    Decoder                 decoder;
    Decoded                 d;
    if (!decoder.decode(call, d) || d.insn.mnemonic != ZYDIS_MNEMONIC_CALL) {
        return out;
    }
    std::uintptr_t const start = d.next();
    // where the result is kept (copied from rax to a register or a stack slot), and the values it is compared with
    std::set<std::int64_t>    holders{ZYDIS_REGISTER_RAX};
    std::vector<std::int64_t> values;
    std::uintptr_t            at = start;
    for (std::size_t n = 0; n < limit && decoder.decode(at, d) && d.insn.mnemonic != ZYDIS_MNEMONIC_RET; ++n, at = d.next()) {
        auto const& op = d.operands;
        if (isMove(d.insn.mnemonic) && holders.contains(locationOf(op[1])) && locationOf(op[0])) {
            holders.insert(locationOf(op[0]));
        }
        if (d.insn.mnemonic == ZYDIS_MNEMONIC_CMP && op[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && holders.contains(locationOf(op[0])) &&
            std::ranges::find(values, op[1].imm.value.s) == values.end()) {
            values.push_back(op[1].imm.value.s);
        }
    }
    // each value's way through the compares: the constants it moves, by where to (a byte's place, a word's)
    if (values.empty()) {
        return out;
    }
    // and last, a value none of the compares names: the default's way
    values.push_back(*std::ranges::max_element(values) + 1);
    for (std::int64_t const value : values) {
        ResultCase          c{value, {}};
        std::optional<bool> equal;
        at = start;
        for (std::size_t n = 0; n < limit && decoder.decode(at, d) && d.insn.mnemonic != ZYDIS_MNEMONIC_RET; ++n) {
            auto const&    op   = d.operands;
            std::uintptr_t next = d.next();
            if (d.insn.mnemonic == ZYDIS_MNEMONIC_CMP && op[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
                equal = holders.contains(locationOf(op[0])) ? std::optional<bool>(op[1].imm.value.s == value) : std::nullopt;
            } else if (d.insn.mnemonic == ZYDIS_MNEMONIC_JZ || d.insn.mnemonic == ZYDIS_MNEMONIC_JNZ) {
                if (!equal) {
                    break;
                }
                if (*equal == (d.insn.mnemonic == ZYDIS_MNEMONIC_JZ)) {
                    next = targetOf(d, 0);
                }
            } else if (d.insn.mnemonic == ZYDIS_MNEMONIC_JMP && isDirectBranch(d)) {
                next = targetOf(d, 0);
            } else if (d.insn.mnemonic == ZYDIS_MNEMONIC_MOV && op[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && locationOf(op[0])) {
                c.moved.push_back({op[0].size, locationOf(op[0]), op[1].imm.value.s});
            }
            at = next;
        }
        out.push_back(std::move(c));
    }
    return out;
}

std::uintptr_t forwardedTo(std::uintptr_t fn) {
    Decoder decoder;
    Decoded d;
    if (!decoder.decode(fn, d)) {
        return 0;
    }
    auto const& op = d.operands;
    if (d.insn.mnemonic == ZYDIS_MNEMONIC_MOV && op[0].type == ZYDIS_OPERAND_TYPE_REGISTER && op[1].type == ZYDIS_OPERAND_TYPE_REGISTER &&
        ((op[0].reg.value == ZYDIS_REGISTER_RCX && op[1].reg.value == ZYDIS_REGISTER_RDX) || (op[0].reg.value == ZYDIS_REGISTER_RDI && op[1].reg.value == ZYDIS_REGISTER_RSI))) {
        if (!decoder.decode(d.next(), d)) {
            return 0;
        }
    }
    if (d.insn.mnemonic != ZYDIS_MNEMONIC_JMP || !isDirectBranch(d)) {
        return 0;
    }
    std::uintptr_t const to = targetOf(d, 0);
    return inCode(to) ? to : 0;
}

std::vector<std::uintptr_t> branchTargets(std::uintptr_t fn, std::uintptr_t end, std::size_t limit, bool toExit) {
    std::vector<std::uintptr_t> out;
    Decoder                     decoder;
    Decoded                     d;
    std::uintptr_t              at = fn;
    for (std::size_t n = 0; n < limit && at < end && decoder.decode(at, d); ++n, at = d.next()) {
        if (isDirectBranch(d)) {
            if (std::uintptr_t const to = targetOf(d, 0); inCode(to)) {
                out.push_back(to);
            }
        }
        if (toExit && (d.insn.mnemonic == ZYDIS_MNEMONIC_RET || d.insn.mnemonic == ZYDIS_MNEMONIC_JMP)) {
            break;
        }
    }
    return out;
}

std::uintptr_t throughThunk(std::uintptr_t fn) {
    Decoder decoder;
    Decoded d;
    for (int hops = 0; hops < 8 && decoder.decode(fn, d) && d.insn.mnemonic == ZYDIS_MNEMONIC_JMP && isDirectBranch(d); ++hops) {
        std::uintptr_t const to = targetOf(d, 0);
        if (!inCode(to)) {
            break;
        }
        fn = to;
    }
    return fn;
}

bool returnsConstant(std::uintptr_t fn) {
    Decoder        decoder;
    Decoded        d;
    bool           constant = false;
    std::uintptr_t at       = fn;
    for (int n = 0; n < 8 && decoder.decode(at, d); ++n, at = d.next()) {
        auto const& op = d.operands;
        if (d.insn.mnemonic == ZYDIS_MNEMONIC_RET) {
            return constant;
        }
        if (d.insn.mnemonic == ZYDIS_MNEMONIC_MOV && op[0].type == ZYDIS_OPERAND_TYPE_REGISTER && op[0].reg.value == ZYDIS_REGISTER_EAX &&
            op[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
            constant = true;
        } else if ((d.insn.mnemonic == ZYDIS_MNEMONIC_PUSH || d.insn.mnemonic == ZYDIS_MNEMONIC_POP) && op[0].reg.value == ZYDIS_REGISTER_RBP) {
            continue;
        } else if (d.insn.mnemonic == ZYDIS_MNEMONIC_MOV && op[0].type == ZYDIS_OPERAND_TYPE_REGISTER && op[0].reg.value == ZYDIS_REGISTER_RBP &&
                   op[1].reg.value == ZYDIS_REGISTER_RSP) {
            continue;
        } else if (op[0].type == ZYDIS_OPERAND_TYPE_MEMORY && (d.insn.mnemonic == ZYDIS_MNEMONIC_MOV || d.insn.mnemonic == ZYDIS_MNEMONIC_MOVSS)) {
            continue;
        } else {
            return false;
        }
    }
    return false;
}

bool returnsMember(std::uintptr_t fn) {
    Decoder decoder;
    Decoded d;
    if (!decoder.decode(fn, d)) {
        return false;
    }
    auto const& op = d.operands;
    if (d.insn.mnemonic != ZYDIS_MNEMONIC_MOV || op[0].type != ZYDIS_OPERAND_TYPE_REGISTER || op[0].reg.value != ZYDIS_REGISTER_EAX || op[1].type != ZYDIS_OPERAND_TYPE_MEMORY ||
        (op[1].mem.base != ZYDIS_REGISTER_RCX && op[1].mem.base != ZYDIS_REGISTER_RDI) || op[1].mem.index != ZYDIS_REGISTER_NONE) {
        return false;
    }
    return decoder.decode(d.next(), d) && d.insn.mnemonic == ZYDIS_MNEMONIC_RET;
}

namespace {

ZydisRegister widest(ZydisRegister r) { return ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, r); }

}

std::vector<MemberSwitch> memberSwitches(Function fn) {
#ifdef _WIN32
    std::set<ZydisRegister> self{ZYDIS_REGISTER_RCX};
    ZydisRegister const volatiles[] = {ZYDIS_REGISTER_RAX, ZYDIS_REGISTER_RCX, ZYDIS_REGISTER_RDX, ZYDIS_REGISTER_R8, ZYDIS_REGISTER_R9, ZYDIS_REGISTER_R10, ZYDIS_REGISTER_R11};
#else
    std::set<ZydisRegister> self{ZYDIS_REGISTER_RDI};
    ZydisRegister const     volatiles[] = {ZYDIS_REGISTER_RAX, ZYDIS_REGISTER_RCX, ZYDIS_REGISTER_RDX, ZYDIS_REGISTER_RSI, ZYDIS_REGISTER_RDI,
                                           ZYDIS_REGISTER_R8,  ZYDIS_REGISTER_R9,  ZYDIS_REGISTER_R10, ZYDIS_REGISTER_R11};
#endif
    std::set<std::pair<ZydisRegister, std::int64_t>> slots;  // stack slots holding `this`
    std::vector<MemberSwitch>                        out;
    Decoder                                          decoder;
    Decoded                                          d;
    std::uintptr_t                                   at = fn.begin;
    for (int n = 0; n < 600 && at < fn.end && decoder.decode(at, d); ++n, at = d.next()) {
        auto const& op = d.operands;
        if (isDirectBranch(d) && d.insn.mnemonic == ZYDIS_MNEMONIC_CALL) {
            for (ZydisRegister r : volatiles) {
                self.erase(r);
            }
            continue;
        }
        if (d.insn.mnemonic != ZYDIS_MNEMONIC_MOV) {
            continue;
        }
        if (op[0].type == ZYDIS_OPERAND_TYPE_MEMORY && op[1].type == ZYDIS_OPERAND_TYPE_REGISTER && op[0].size == 64 &&
            (op[0].mem.base == ZYDIS_REGISTER_RSP || op[0].mem.base == ZYDIS_REGISTER_RBP) && op[0].mem.index == ZYDIS_REGISTER_NONE) {
            std::pair const slot{op[0].mem.base, op[0].mem.disp.value};
            if (self.contains(widest(op[1].reg.value))) {
                slots.insert(slot);
            } else {
                slots.erase(slot);
            }
            continue;
        }
        if (op[0].type != ZYDIS_OPERAND_TYPE_REGISTER) {
            continue;
        }
        ZydisRegister const to = widest(op[0].reg.value);
        if (op[1].type == ZYDIS_OPERAND_TYPE_REGISTER && op[0].size == 64) {
            if (self.contains(widest(op[1].reg.value))) {
                self.insert(to);
            } else {
                self.erase(to);
            }
        } else if (op[1].type == ZYDIS_OPERAND_TYPE_MEMORY && op[0].size == 64 && op[1].mem.index == ZYDIS_REGISTER_NONE &&
                   slots.contains({op[1].mem.base, op[1].mem.disp.value})) {
            self.insert(to);
        } else if (op[1].type == ZYDIS_OPERAND_TYPE_MEMORY && op[0].size == 32 && self.contains(op[1].mem.base) && op[1].mem.index == ZYDIS_REGISTER_NONE &&
                   op[1].mem.disp.value > 0x40) {
            out.push_back(MemberSwitch{static_cast<std::int32_t>(op[1].mem.disp.value), d.next(), static_cast<int>(to)});
            self.erase(to);
        } else {
            self.erase(to);
        }
    }
    return out;
}

std::vector<MemberSwitch> resultSwitches(Function fn) {
    std::vector<MemberSwitch> out;
    Decoder                   decoder;
    Decoded                   d, next;
    for (std::uintptr_t at = fn.begin; at < fn.end;) {
        if (!decoder.decode(at, d)) {
            ++at;
            continue;
        }
        at = d.next();
        if (!isDirectBranch(d) || d.insn.mnemonic != ZYDIS_MNEMONIC_CALL || !decoder.decode(at, next)) {
            continue;
        }
        auto const& op      = next.operands;
        bool const  fromEax = op[0].type == ZYDIS_OPERAND_TYPE_REGISTER && widest(op[0].reg.value) == ZYDIS_REGISTER_RAX &&
                             (next.insn.mnemonic == ZYDIS_MNEMONIC_CMP || next.insn.mnemonic == ZYDIS_MNEMONIC_SUB || next.insn.mnemonic == ZYDIS_MNEMONIC_TEST);
        bool const moved = next.insn.mnemonic == ZYDIS_MNEMONIC_MOV && op[0].type == ZYDIS_OPERAND_TYPE_REGISTER && op[0].size == 32 && op[1].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                           op[1].reg.value == ZYDIS_REGISTER_EAX;
        if (fromEax) {
            out.push_back({0, at, static_cast<int>(ZYDIS_REGISTER_RAX), targetOf(d, 0)});
        } else if (moved) {
            out.push_back({0, next.next(), static_cast<int>(widest(op[0].reg.value)), targetOf(d, 0)});
        }
    }
    return out;
}

std::uintptr_t followSwitch(MemberSwitch const& s, std::int64_t value, std::vector<std::uintptr_t> const& stops) {
    Decoder                      decoder;
    Decoded                      d;
    std::optional<std::uint32_t> v = static_cast<std::uint32_t>(value);
    // the last compare's operands, as `a - b`
    std::optional<std::pair<std::uint32_t, std::uint32_t>> flags;
    std::uintptr_t                                         at = s.after;
    for (int n = 0; n < 100 && decoder.decode(at, d); ++n) {
        auto const&    op      = d.operands;
        bool const     onValue = op[0].type == ZYDIS_OPERAND_TYPE_REGISTER && widest(op[0].reg.value) == static_cast<ZydisRegister>(s.reg);
        std::uintptr_t next    = d.next();
        switch (d.insn.mnemonic) {
        case ZYDIS_MNEMONIC_SUB:
        case ZYDIS_MNEMONIC_CMP:
        case ZYDIS_MNEMONIC_ADD:
            if (onValue && v && op[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
                auto const imm = static_cast<std::uint32_t>(op[1].imm.value.s);
                flags          = d.insn.mnemonic == ZYDIS_MNEMONIC_ADD ? std::pair{*v + imm, 0u} : std::pair{*v, imm};
                if (d.insn.mnemonic == ZYDIS_MNEMONIC_SUB) {
                    v = *v - imm;
                }
                if (d.insn.mnemonic == ZYDIS_MNEMONIC_ADD) {
                    v = *v + imm;
                }
            } else {
                flags.reset();
                if (onValue) {
                    v.reset();
                }
            }
            break;
        case ZYDIS_MNEMONIC_TEST:
            if (onValue && v && op[1].type == ZYDIS_OPERAND_TYPE_REGISTER && widest(op[1].reg.value) == static_cast<ZydisRegister>(s.reg)) {
                flags = std::pair{*v, 0u};
            } else {
                flags.reset();
            }
            break;
        case ZYDIS_MNEMONIC_JZ:
        case ZYDIS_MNEMONIC_JNZ:
        case ZYDIS_MNEMONIC_JB:
        case ZYDIS_MNEMONIC_JNB:
        case ZYDIS_MNEMONIC_JBE:
        case ZYDIS_MNEMONIC_JNBE:
        case ZYDIS_MNEMONIC_JL:
        case ZYDIS_MNEMONIC_JNL:
        case ZYDIS_MNEMONIC_JLE:
        case ZYDIS_MNEMONIC_JNLE: {
            if (!flags) {
                return 0;
            }
            auto const [a, b] = *flags;
            auto const sa = static_cast<std::int32_t>(a), sb = static_cast<std::int32_t>(b);
            bool       taken = false;
            switch (d.insn.mnemonic) {
            case ZYDIS_MNEMONIC_JZ:
                taken = a == b;
                break;
            case ZYDIS_MNEMONIC_JNZ:
                taken = a != b;
                break;
            case ZYDIS_MNEMONIC_JB:
                taken = a < b;
                break;
            case ZYDIS_MNEMONIC_JNB:
                taken = a >= b;
                break;
            case ZYDIS_MNEMONIC_JBE:
                taken = a <= b;
                break;
            case ZYDIS_MNEMONIC_JNBE:
                taken = a > b;
                break;
            case ZYDIS_MNEMONIC_JL:
                taken = sa < sb;
                break;
            case ZYDIS_MNEMONIC_JNL:
                taken = sa >= sb;
                break;
            case ZYDIS_MNEMONIC_JLE:
                taken = sa <= sb;
                break;
            default:
                taken = sa > sb;
                break;
            }
            if (taken) {
                next = targetOf(d, 0);
            }
            break;
        }
        case ZYDIS_MNEMONIC_JMP:
            if (!isDirectBranch(d)) {
                return 0;
            }
            if (std::ranges::count(stops, targetOf(d, 0))) {
                return targetOf(d, 0);
            }
            next = targetOf(d, 0);
            break;
        case ZYDIS_MNEMONIC_CALL:
            if (isDirectBranch(d) && std::ranges::count(stops, targetOf(d, 0))) {
                return targetOf(d, 0);
            }
            v.reset();
            flags.reset();
            break;
        case ZYDIS_MNEMONIC_RET:
            return 0;
        case ZYDIS_MNEMONIC_LEA:
            if (op[1].type == ZYDIS_OPERAND_TYPE_MEMORY && op[1].mem.base == ZYDIS_REGISTER_RIP && std::ranges::count(stops, targetOf(d, 1))) {
                return targetOf(d, 1);
            }
            if (onValue) {
                v.reset();
            }
            break;
        case ZYDIS_MNEMONIC_MOV:
        case ZYDIS_MNEMONIC_MOVZX:
        case ZYDIS_MNEMONIC_MOVSXD:
        case ZYDIS_MNEMONIC_MOVSS:
        case ZYDIS_MNEMONIC_MOVSD:
        case ZYDIS_MNEMONIC_MOVAPS:
        case ZYDIS_MNEMONIC_MOVUPS:
        case ZYDIS_MNEMONIC_NOP:
            if (onValue) {
                v.reset();
            }
            break;
        default:
            // anything else may change the flags, and the member's register where it writes it
            flags.reset();
            if (onValue) {
                v.reset();
            }
        }
        at = next;
    }
    return 0;
}

bool onlyReads(std::uintptr_t fn) {
    Decoder        decoder;
    Decoded        d;
    std::uintptr_t at = fn;
    for (int n = 0; n < 24 && decoder.decode(at, d); ++n, at = d.next()) {
        if (d.insn.mnemonic == ZYDIS_MNEMONIC_RET) {
            return true;
        }
        if (d.insn.meta.category == ZYDIS_CATEGORY_CALL) {
            return false;
        }
        for (int i = 0; i < d.insn.operand_count_visible; ++i) {
            auto const& op = d.operands[i];
            if (op.type == ZYDIS_OPERAND_TYPE_MEMORY && (op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) && op.mem.base != ZYDIS_REGISTER_RSP) {
                return false;
            }
        }
    }
    return false;
}

bool isTrivial(std::uintptr_t fn) {
    Decoder decoder;
    Decoded d;
    if (!decoder.decode(fn, d)) {
        return false;
    }
    if (d.insn.mnemonic == ZYDIS_MNEMONIC_JMP) {
        return true;
    }
    return decoder.decode(d.next(), d) && d.insn.mnemonic == ZYDIS_MNEMONIC_RET;
}

namespace {

bool isFirstArgument(ZydisDecodedOperand const& op) {
    return op.type == ZYDIS_OPERAND_TYPE_REGISTER &&
           (op.reg.value == ZYDIS_REGISTER_RCX || op.reg.value == ZYDIS_REGISTER_RDI || op.reg.value == ZYDIS_REGISTER_ECX || op.reg.value == ZYDIS_REGISTER_EDI);
}

// what the instruction adds to the first argument register: lea rcx, [reg + d] / add rcx, d / mov rcx, reg (0)
std::optional<std::int32_t> argumentOffset(Decoded const& d) {
    auto const& op = d.operands;
    if (!isFirstArgument(op[0])) {
        return std::nullopt;
    }
    if (d.insn.mnemonic == ZYDIS_MNEMONIC_LEA && op[1].type == ZYDIS_OPERAND_TYPE_MEMORY && op[1].mem.base != ZYDIS_REGISTER_RIP && op[1].mem.index == ZYDIS_REGISTER_NONE) {
        return static_cast<std::int32_t>(op[1].mem.disp.value);
    }
    if (d.insn.mnemonic == ZYDIS_MNEMONIC_ADD && op[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
        return static_cast<std::int32_t>(op[1].imm.value.s);
    }
    if (d.insn.mnemonic == ZYDIS_MNEMONIC_MOV && op[1].type == ZYDIS_OPERAND_TYPE_REGISTER) {
        return 0;
    }
    return std::nullopt;
}

}

std::vector<ByteArgument> byteArgumentsTo(std::uintptr_t callee, Function fn) {
    struct Found {
        std::uintptr_t source;
        std::int32_t   offset;
    };
    std::vector<Found>                               found;
    std::map<std::uintptr_t, std::set<std::int32_t>> arguments;  // each call target's first-argument offsets
    Decoder                                          decoder;
    Decoded                                          d, previous{};
    std::uintptr_t                                   lastCall = 0;
    std::optional<std::int32_t>                      byteAt;             // a byte just read into the first argument
    int                                              since        = 99;  // instructions since it
    bool                                             havePrevious = false;
    for (std::uintptr_t at = fn.begin; at < fn.end;) {
        if (!decoder.decode(at, d)) {
            ++at;
            havePrevious = false;
            continue;
        }
        auto const& op = d.operands;
        ++since;
        if (isDirectBranch(d) && d.insn.mnemonic == ZYDIS_MNEMONIC_CALL) {
            std::uintptr_t const to = targetOf(d, 0);
            if (to == callee) {
                if (byteAt && since <= 4 && lastCall) {
                    found.push_back({lastCall, *byteAt});
                }
            } else {
                lastCall          = to;
                auto const offset = havePrevious ? argumentOffset(previous) : std::nullopt;
                arguments[to].insert(offset.value_or(0));
            }
            byteAt.reset();
        } else if (d.insn.mnemonic == ZYDIS_MNEMONIC_MOVZX && isFirstArgument(op[0]) && op[1].type == ZYDIS_OPERAND_TYPE_MEMORY && op[1].size == 8 &&
                   op[1].mem.base != ZYDIS_REGISTER_RIP && op[1].mem.index == ZYDIS_REGISTER_NONE) {
            byteAt = static_cast<std::int32_t>(op[1].mem.disp.value);
            since  = 0;
        } else if (isFirstArgument(op[0]) && (op[0].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE)) {
            byteAt.reset();
        }
        previous     = d;
        havePrevious = true;
        at           = d.next();
    }
    std::vector<ByteArgument> out;
    for (Found const& f : found) {
        out.push_back({f.source, f.offset, {arguments[f.source].begin(), arguments[f.source].end()}});
    }
    return out;
}

}
