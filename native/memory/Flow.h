#pragma once
// What a member function of the image does with `this`, read from its code (decoded with Zydis, in one pass from its
// start: no branch is followed, which is enough for the short member functions asked about).

#include <Zydis/Zydis.h>

#include <cstdint>
#include <vector>

namespace bdg::memory {

struct Instruction {
    std::uintptr_t          at;
    ZydisDecodedInstruction insn;
    ZydisDecodedOperand     ops[ZYDIS_MAX_OPERAND_COUNT];
};

// The instructions of [from, to) of the function [begin, end), decoded from its start (a byte that decodes as none
// skipped); none when it cannot be read
std::vector<Instruction> decode(std::uintptr_t begin, std::uintptr_t end, std::uintptr_t from, std::uintptr_t to);

// the 64-bit general register of any of its widths' names (rax 0 to r15 15); -1 for others
int gpr(ZydisRegister reg);

// Whether the function [begin, end) calls a virtual of the object its first argument (`this`) points to at `member`:
// `this`, then [this + member], then its vtable, then [vtable + slot * 8] for one of `slots`, called (directly, or through
// Control Flow Guard's dispatch, which takes the target in rax) or jumped to.
bool callsVirtualOfMember(std::uintptr_t begin, std::uintptr_t end, std::int64_t member, std::vector<int> const& slots);

// Whether the member function [begin, end), which returns its result through a hidden pointer, calls a virtual of
// `this` itself: its vtable, then [vtable + slot * 8] for one of `slots`, called or jumped to as above.
bool callsVirtualOfThisReturning(std::uintptr_t begin, std::uintptr_t end, std::vector<int> const& slots);

// Whether the function at `fn` calls nothing: on Windows, it has no unwind data (a leaf function: no frame, no call); on
// Linux, its code has no call and no jump out of it
bool callsNothing(std::uintptr_t fn);

// Whether the function [begin, end) writes to memory through its first argument (`this`, or a register it is copied
// to): what a const member function does not (a constructor writes its vtable there)
bool writesThroughThis(std::uintptr_t begin, std::uintptr_t end);

}
