#pragma once
// The server's code read as code, for what no binding names: where its functions begin (Windows: the image's unwind
// table, whose leaf functions have no entry; Linux: the executable's .eh_frame_hdr, which lists every function), the
// instructions that refer to an address, and what a function's instructions refer to (decoded by Zydis).

#include <cstdint>
#include <optional>
#include <vector>

namespace bdg::memory {

struct Function {
    std::uintptr_t begin = 0, end = 0;  // end: where the next function begins
    explicit       operator bool() const { return begin != 0; }
};

// The function holding `address`: on Windows the primary part of one the compiler split (its cold parts are chained
// to it); none where the unwind data has no entry there.
Function functionAt(std::uintptr_t address);

// The function before `fn` in the image (Windows: the primary one before it); none at the first.
Function functionBefore(Function fn);

// Every function listed, in address order (Windows: the primary ones).
std::vector<Function> functionsIn(std::uintptr_t from, std::uintptr_t to);

enum class Ref { Call, Jump, Lea, Load, Absolute };

struct Reference {
    std::uintptr_t at;  // the instruction
    Ref            kind;
};

// Every `call rel32`, `jmp rel32`, rip-relative `lea` and rip-relative load (`mov r32/r64`, `movups`, `movaps`,
// `movdqu`: a string literal copied in place) in the code whose target is `target`; in an image loaded below 2 GiB, also
// every imm32 that is the address itself (Absolute, at the immediate).
std::vector<Reference> referencesTo(std::uintptr_t target);

// The targets of the function's rip-relative `lea`s inside the code (with `data`, outside it: a vtable, a constant), in
// the order its instructions hold them.
std::vector<std::uintptr_t> leaTargets(Function fn, bool data = false);

// The data every rip-relative memory operand of the function points at (a `lea`'s address, a load's source), and every
// immediate that is an address in the image's data, outside the code, in the order its instructions hold them.
std::vector<std::uintptr_t> dataTargets(Function fn);

// Where a function that only passes its arguments on goes: `jmp X`, or `mov rcx, rdx` / `mov rdi, rsi` then `jmp X` (a
// method handing its argument to a free function); 0 where it is none.
std::uintptr_t forwardedTo(std::uintptr_t fn);

// The targets of the function's `call rel32` and `jmp rel32`, the first `limit` instructions of it; with `toExit`, up to
// its first `ret` or `jmp` (a small function's code runs on into the next one's).
std::vector<std::uintptr_t> branchTargets(std::uintptr_t fn, std::uintptr_t end, std::size_t limit = 400, bool toExit = false);

// Where a `jmp rel32` thunk at `fn` goes, through a thunk it goes to as well (Linux 1.21.60's tint functions are two
// thunks away from their samplers); `fn` itself where it is no thunk.
std::uintptr_t throughThunk(std::uintptr_t fn);

// Whether `fn` only returns a constant: `mov eax, imm32` and `ret`, with nothing between but a frame and stores (an
// unoptimized build's).
bool returnsConstant(std::uintptr_t fn);

// Whether `fn` only returns a member of its object: `mov eax, [rcx|rdi + disp]` and `ret`.
bool returnsMember(std::uintptr_t fn);

// Whether `fn` only reads: up to its first `ret`, no call and no write but to its own stack frame.
bool onlyReads(std::uintptr_t fn);

// Whether `fn` is at most two instructions, the last a `ret`, or starts with a `jmp`: a getter or a thunk.
bool isTrivial(std::uintptr_t fn);

// A function that picks what it calls by a member of its object: each `mov r32, dword ptr [this + offset]` in it (`this`
// the first argument, or a register or stack slot it was copied to), and where the code goes on from each.
struct MemberSwitch {
    std::int32_t   offset = 0;
    std::uintptr_t after  = 0;  // the instruction after the load
    int            reg    = 0;  // the register loaded (a ZydisRegister)
    std::uintptr_t source = 0;  // resultSwitches: the function whose result is switched on (in eax), else 0
};
std::vector<MemberSwitch> memberSwitches(Function fn);

// A function that picks what it does by what a call returns: each `call X` whose result the next instructions compare
// (`mov ecx, eax`, or a `cmp`/`sub`/`test` of eax).
std::vector<MemberSwitch> resultSwitches(Function fn);

// The first of `stops` the code from the load calls, jumps to or takes the address of (a rip-relative `lea`) when the
// member holds `value`: its compares and branches on the member followed, anything else passed over; 0 where it gets to
// none, or to a branch on what it does not know.
std::uintptr_t followSwitch(MemberSwitch const& s, std::int64_t value, std::vector<std::uintptr_t> const& stops);

// A switch on what the call at `call` returns, as a chain of compares (MSVC's in order, clang's as it likes, an unoptimized
// build's on a stack slot): each value the result (or a register or stack slot it is copied to) is compared with, and
// the constants the code moves to a register or a stack slot on that value's way, its compares and branches followed,
// up to a `ret` or `limit` instructions; last, the way of a value none of them names (one past the greatest: the
// default). The calls on the way are passed over (a case's code may call).
struct ResultCase {
    std::int64_t compared = 0;
    struct Moved {
        int          bits;      // the destination's width
        std::int64_t location;  // the register or stack slot (equal where the same)
        std::int64_t value;
    };
    std::vector<Moved> moved;
};
std::vector<ResultCase> casesOfResult(std::uintptr_t call, std::size_t limit = 60);

// The calls in `fn` to `callee` whose first argument is a byte read just before (`movzx ecx|edi, byte ptr [reg + disp]`,
// a few instructions before the call) from what the last call before it returned: that call's target, the byte's offset,
// and every offset `fn` adds to the first argument of a call to that target (`lea rcx|rdi, [reg + d]`, `add rcx|rdi, d`;
// 0 where it passes it as it is).
struct ByteArgument {
    std::uintptr_t            source;
    std::int32_t              offset;
    std::vector<std::int32_t> sourceArguments;
};
std::vector<ByteArgument> byteArgumentsTo(std::uintptr_t callee, Function fn);

}
