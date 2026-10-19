#include "memory/Flow.h"

#include "memory/Mem.h"

#include <Zydis/Zydis.h>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <utility>

namespace bdg::memory {

namespace {

// what a register (or a spilled stack slot) holds of the chain this -> member -> vtable -> function
enum class Holds { Nothing, This, Member, Vtable, Function };

constexpr int Rax = 0, Rcx = 1, Rdx = 2, Rsp = 4, Rbp = 5, Rsi = 6, Rdi = 7;

bool callsVirtual(std::uintptr_t begin, std::uintptr_t end, int thisReg, std::optional<std::int64_t> member, std::vector<int> const& slots);

}

int gpr(ZydisRegister reg) {
    ZydisRegister const wide = ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, reg);
    if (wide < ZYDIS_REGISTER_RAX || wide > ZYDIS_REGISTER_R15) {
        return -1;
    }
    return static_cast<int>(wide - ZYDIS_REGISTER_RAX);
}

std::vector<Instruction> decode(std::uintptr_t begin, std::uintptr_t end, std::uintptr_t from, std::uintptr_t to) {
    std::vector<Instruction>  out;
    ZydisDecoder              decoder;
    std::vector<std::uint8_t> code(static_cast<std::size_t>(std::min(end, to) - begin));
    if (code.empty() || !ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64)) ||
        !safeRead(reinterpret_cast<void const*>(begin), code.data(), code.size())) {
        return out;
    }
    for (std::size_t at = 0; at < code.size();) {
        Instruction x{begin + at, {}, {}};
        // the instructions before `from` only for where the next one starts
        bool const kept = begin + at >= from;
        if (!ZYAN_SUCCESS(kept ? ZydisDecoderDecodeFull(&decoder, code.data() + at, code.size() - at, &x.insn, x.ops)
                               : ZydisDecoderDecodeInstruction(&decoder, nullptr, code.data() + at, code.size() - at, &x.insn))) {
            ++at;
            continue;
        }
        at += x.insn.length;
        if (kept) {
            out.push_back(x);
        }
    }
    return out;
}

bool callsVirtualOfMember(std::uintptr_t begin, std::uintptr_t end, std::int64_t member, std::vector<int> const& slots) {
#ifdef _WIN32
    return callsVirtual(begin, end, Rcx, member, slots);
#else
    return callsVirtual(begin, end, Rdi, member, slots);
#endif
}

bool callsVirtualOfThisReturning(std::uintptr_t begin, std::uintptr_t end, std::vector<int> const& slots) {
    // the hidden pointer: after `this` with MSVC, before it on Linux
#ifdef _WIN32
    return callsVirtual(begin, end, Rcx, std::nullopt, slots);
#else
    return callsVirtual(begin, end, Rsi, std::nullopt, slots);
#endif
}

namespace {

// Whether [begin, end) calls a virtual of the object `this` (in `thisReg`) points to at `member`, or of `this` itself
bool callsVirtual(std::uintptr_t begin, std::uintptr_t end, int thisReg, std::optional<std::int64_t> member, std::vector<int> const& slots) {
    ZydisDecoder decoder;
    if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64))) {
        return false;
    }
    std::vector<std::uint8_t> code(static_cast<std::size_t>(end - begin));
    if (code.empty() || !safeRead(reinterpret_cast<void const*>(begin), code.data(), code.size())) {
        return false;
    }
    std::array<Holds, 16> regs{};
    regs[thisReg] = Holds::This;
#ifdef _WIN32
    // the registers a call may change: MSVC's volatile ones
    constexpr int volatileRegs[] = {Rax, Rcx, Rdx, 8, 9, 10, 11};
#else
    constexpr int volatileRegs[] = {Rax, Rcx, Rdx, Rsi, Rdi, 8, 9, 10, 11};
#endif
    std::map<std::pair<int, std::int64_t>, Holds> spilled;  // by base register (rsp, rbp) and displacement
    // the registers at each conditional jump not yet followed: where a path ends (ret, jmp), the code after it is most
    // often a branch's target, and the registers are as they were at the branch
    std::vector<std::array<Holds, 16>> branches;
    auto isSlot = [&](std::int64_t disp) { return disp >= 0 && disp % 8 == 0 && std::ranges::find(slots, static_cast<int>(disp / 8)) != slots.end(); };
    // what [base + disp] holds, read from where `held` (the base's) is
    auto loaded = [&](Holds held, std::int64_t disp) {
        if (held == Holds::This && member && disp == *member) {
            return Holds::Member;
        }
        if ((held == Holds::Member || (held == Holds::This && !member)) && disp == 0) {
            return Holds::Vtable;
        }
        if (held == Holds::Vtable && isSlot(disp)) {
            return Holds::Function;
        }
        return Holds::Nothing;
    };

    for (std::size_t at = 0; at < code.size();) {
        ZydisDecodedInstruction insn;
        ZydisDecodedOperand     ops[ZYDIS_MAX_OPERAND_COUNT];
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, code.data() + at, code.size() - at, &insn, ops))) {
            ++at;
            continue;
        }
        at += insn.length;
        ZydisDecodedOperand const& a    = ops[0];
        ZydisDecodedOperand const& b    = ops[1];
        bool const                 memB = insn.operand_count_visible >= 2 && b.type == ZYDIS_OPERAND_TYPE_MEMORY && b.mem.index == ZYDIS_REGISTER_NONE;

        if (insn.meta.category == ZYDIS_CATEGORY_COND_BR) {
            branches.push_back(regs);
            continue;
        }
        if (insn.mnemonic == ZYDIS_MNEMONIC_RET) {
            if (!branches.empty()) {
                regs = branches.back();
                branches.pop_back();
            }
            continue;
        }

        if (insn.mnemonic == ZYDIS_MNEMONIC_CALL || insn.mnemonic == ZYDIS_MNEMONIC_JMP) {
            bool calls = false;
            if (a.type == ZYDIS_OPERAND_TYPE_MEMORY && a.mem.index == ZYDIS_REGISTER_NONE) {
                int const base = gpr(a.mem.base);
                // call [vtable + slot * 8]
                calls = base >= 0 && regs[base] == Holds::Vtable && isSlot(a.mem.disp.value);
                // Control Flow Guard: call [rip + __guard_dispatch_icall_fptr] with the target in rax
                calls |= a.mem.base == ZYDIS_REGISTER_RIP && regs[Rax] == Holds::Function;
            } else if (a.type == ZYDIS_OPERAND_TYPE_REGISTER) {
                int const reg = gpr(a.reg.value);
                calls         = reg >= 0 && regs[reg] == Holds::Function;
            }
            if (calls) {
                return true;
            }
            if (insn.mnemonic == ZYDIS_MNEMONIC_CALL) {
                for (int reg : volatileRegs) {
                    regs[reg] = Holds::Nothing;
                }
            } else if (!branches.empty()) {
                regs = branches.back();
                branches.pop_back();
            }
            continue;
        }

        if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && a.type == ZYDIS_OPERAND_TYPE_REGISTER && a.size == 64) {
            int const to = gpr(a.reg.value);
            if (to < 0) {
                continue;
            }
            if (b.type == ZYDIS_OPERAND_TYPE_REGISTER) {
                int const from = gpr(b.reg.value);
                regs[to]       = from >= 0 ? regs[from] : Holds::Nothing;
            } else if (memB) {
                int const base = gpr(b.mem.base);
                // rbp is the frame's base only where it holds nothing of the chain (a function may keep `this` in it)
                if (base == Rsp || (base == Rbp && regs[Rbp] == Holds::Nothing)) {
                    auto const it = spilled.find({base, b.mem.disp.value});
                    regs[to]      = it == spilled.end() ? Holds::Nothing : it->second;
                } else {
                    regs[to] = base >= 0 ? loaded(regs[base], b.mem.disp.value) : Holds::Nothing;
                }
            } else {
                regs[to] = Holds::Nothing;
            }
            continue;
        }

        // a spill of a register to the stack
        if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && a.type == ZYDIS_OPERAND_TYPE_MEMORY && b.type == ZYDIS_OPERAND_TYPE_REGISTER && b.size == 64) {
            int const base = gpr(a.mem.base), from = gpr(b.reg.value);
            if ((base == Rsp || (base == Rbp && regs[Rbp] == Holds::Nothing)) && from >= 0) {
                spilled[{base, a.mem.disp.value}] = regs[from];
            }
            continue;
        }

        // any other instruction: the registers it writes hold nothing of the chain any more
        for (int i = 0; i < insn.operand_count; ++i) {
            if (ops[i].type == ZYDIS_OPERAND_TYPE_REGISTER && (ops[i].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE)) {
                if (int const reg = gpr(ops[i].reg.value); reg >= 0 && reg != Rsp) {
                    regs[reg] = Holds::Nothing;
                }
            }
        }
    }
    return false;
}

}

bool writesThroughThis(std::uintptr_t begin, std::uintptr_t end) {
    std::array<bool, 16> isThis{};
#ifdef _WIN32
    isThis[Rcx]                  = true;
    constexpr int volatileRegs[] = {Rax, Rcx, Rdx, 8, 9, 10, 11};
#else
    isThis[Rdi]                  = true;
    constexpr int volatileRegs[] = {Rax, Rcx, Rdx, Rsi, Rdi, 8, 9, 10, 11};
#endif
    for (Instruction const& x : decode(begin, end, begin, end)) {
        for (int i = 0; i < x.insn.operand_count_visible; ++i) {
            ZydisDecodedOperand const& op   = x.ops[i];
            int const                  base = op.type == ZYDIS_OPERAND_TYPE_MEMORY ? gpr(op.mem.base) : -1;
            if (base >= 0 && op.mem.type == ZYDIS_MEMOP_TYPE_MEM && (op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) && isThis[base]) {
                return true;
            }
        }
        if (x.insn.mnemonic == ZYDIS_MNEMONIC_CALL) {
            for (int reg : volatileRegs) {
                isThis[reg] = false;
            }
            continue;
        }
        ZydisDecodedOperand const& a = x.ops[0];
        ZydisDecodedOperand const& b = x.ops[1];
        if (x.insn.mnemonic == ZYDIS_MNEMONIC_MOV && a.type == ZYDIS_OPERAND_TYPE_REGISTER && b.type == ZYDIS_OPERAND_TYPE_REGISTER && a.size == 64 && gpr(a.reg.value) >= 0 &&
            gpr(b.reg.value) >= 0) {
            isThis[gpr(a.reg.value)] = isThis[gpr(b.reg.value)];
            continue;
        }
        for (int i = 0; i < x.insn.operand_count; ++i) {
            if (x.ops[i].type == ZYDIS_OPERAND_TYPE_REGISTER && (x.ops[i].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE)) {
                if (int const reg = gpr(x.ops[i].reg.value); reg >= 0 && reg != Rsp) {
                    isThis[reg] = false;
                }
            }
        }
    }
    return false;
}

bool callsNothing(std::uintptr_t fn) {
    auto const [begin, end] = functionAround(fn);
#ifdef _WIN32
    return begin == 0 && inImage(fn);
#else
    if (begin != fn) {
        return false;
    }
    for (Instruction const& x : decode(begin, end, begin, end)) {
        if (x.insn.mnemonic == ZYDIS_MNEMONIC_CALL) {
            return false;
        }
        if (x.insn.mnemonic == ZYDIS_MNEMONIC_JMP) {
            ZyanU64 to = 0;
            if (x.ops[0].type != ZYDIS_OPERAND_TYPE_IMMEDIATE || !ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&x.insn, &x.ops[0], x.at, &to)) || to < begin || to >= end) {
                return false;
            }
        }
    }
    return true;
#endif
}

}
