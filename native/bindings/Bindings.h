#pragma once
// What the agent needs to know about one server build: where its functions are, which vtable slots and member offsets
// it uses, and which calling-convention variants apply. Steps.cpp (Windows) and StepsLinux.cpp list steps, each with
// only what changed since the one before; load() applies them oldest first up to the running build. Windows binds by
// patterns; the Linux servers that carry their symbols (1.16.201 - 1.21.2, but 1.17.0) by name.

#include "core/Version.h"
#include "memory/Scanner.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace bdg::bindings {

class Registry {
public:
    // A function (or, with data(), a global) found by a byte pattern that must match exactly once in .text.
    // rel32At < 0: the match is the address itself. rel32At = n: bytes n..n+3 of the match hold a rel32 /
    // disp32 relative to the end of those 4 bytes (e.g. 1 for `E8 call`, 3 for `48 8D 0D lea`), and the
    // target is the address.
    void function(std::string_view name, std::string_view pattern, int rel32At = -1);
    void data(std::string_view name, std::string_view pattern, int rel32At) { function(name, pattern, rel32At); }
    void slot(std::string_view name, int index);
    // Linux: a function or global by its mangled symbol
    void symbol(std::string_view name, std::string_view mangled);
    // Linux: the slot at which the vtable `vtable` (_ZTV...) holds the function `function` (both mangled)
    void slotOf(std::string_view name, std::string_view vtable, std::string_view function);
    // Linux: the member offset the getter `getter` (mangled) reads from `this` (mov / lea / movzx rax, [rdi + imm]),
    // plus `adjust`: a member the getter reads beside (the permutations' vector 24 bytes before the default state)
    void offsetOf(std::string_view name, std::string_view getter, std::int64_t adjust = 0);
    // Linux: the member offset in the first instruction of `function`'s first 64 bytes that starts with `prefix` (bytes in
    // hex, then a disp32: "48 89 88" is mov [rax + disp32], rcx), for a member no getter reads first
    void offsetIn(std::string_view name, std::string_view function, std::string_view prefix);
    void offset(std::string_view name, std::int64_t value);
    void variant(std::string_view name, int value);
    void drop(std::string_view name);

    // Linux builds without symbols (variant LayoutByContent): what game/Layout.cpp finds by content in the running
    // server, set before anything reads it, and checked there on what the game is known to answer
    void foundOffset(std::string_view name, std::int64_t value);
    void foundFunction(std::string_view name, std::uintptr_t address);

    // Resolves every function against `module`; returns the names that did not resolve to exactly one address.
    std::vector<std::string> resolve(memory::Module const& module);

    // What a server's symbols say, for the name bindings: a symbol's address relative to the image base (0 where
    // it has none), the code or data bytes at an address, an 8-byte word as loaded (relocated).
    struct Symbols {
        virtual ~Symbols()                                                                                    = default;
        virtual std::map<std::string, std::uint64_t> addresses(std::vector<std::string> const& mangled) const = 0;
        virtual bool                                 bytes(std::uint64_t rva, void* out, std::size_t n) const = 0;
        virtual std::uint64_t                        word(std::uint64_t rva) const                            = 0;
    };
    // Resolves every name binding (symbol, slotOf, offsetOf); returns those that did not resolve.
    std::vector<std::string> resolveNames(Symbols const& symbols);

    [[nodiscard]] bool           has(std::string_view name) const;      // a resolved function
    [[nodiscard]] std::uint64_t  rvaOf(std::string_view name) const;    // 0 when unknown
    [[nodiscard]] std::uintptr_t address(std::string_view name) const;  // in the running server; 0 when unknown
    [[nodiscard]] int            slot(std::string_view name) const;     // -1 when unknown
    [[nodiscard]] std::int64_t   offset(std::string_view name, std::int64_t fallback = -1) const;
    [[nodiscard]] int            variant(std::string_view name, int fallback = 0) const;

    // name -> "0x1234 (step 1.21.50)", "slot 7", ... for logs and bdg_check
    [[nodiscard]] std::map<std::string, std::string> describe() const;

    std::string currentStep;

private:
    struct Function {
        std::string   pattern;
        int           rel32At = -1;
        std::string   step;
        std::uint32_t resolved = 0;
    };
    struct Named {
        std::string   symbol, function;  // symbol: the global or getter or vtable; function: what slotOf looks for, or offsetIn's prefix
        std::int64_t  adjust = 0;
        std::string   step;
        std::uint64_t resolved = 0;
    };
    std::map<std::string, Function, std::less<>>     mFunctions;
    std::map<std::string, Named, std::less<>>        mSymbols, mSlotsOf, mOffsetsOf, mOffsetsIn;
    std::map<std::string, int, std::less<>>          mSlots;
    std::map<std::string, std::int64_t, std::less<>> mOffsets;
    std::map<std::string, int, std::less<>>          mVariants;
    std::set<std::string, std::less<>>               mFound;
};

using StepFn = void (*)(Registry&);

struct Step {
    Version     since;
    char const* name;
    StepFn      apply;
};

int addStep(char const* since, StepFn fn);

// A step's registration, a static object of its file. The Linux agent loads the bindings from its own constructor, so
// the registrations must run first (init_priority).
struct StepRegistration {
    StepRegistration(char const* since, StepFn fn) { addStep(since, fn); }
};

// Applies every step up to `running`. Call once; the registry is read-only afterwards.
Registry& load(Version running);

Registry& current();

}

#if defined(__GNUC__) && !defined(_WIN32)
#define BDG_BINDINGS_EARLY __attribute__((init_priority(101)))
#else
#define BDG_BINDINGS_EARLY
#endif

// clang-format off
#define BDG_BINDINGS_CAT2(a, b) a##b
#define BDG_BINDINGS_CAT(a, b)  BDG_BINDINGS_CAT2(a, b)
#define BDG_BINDINGS(since)                                                                              \
    static void BDG_BINDINGS_CAT(bdgStep_, __LINE__)(::bdg::bindings::Registry & r);                    \
    [[maybe_unused]] static ::bdg::bindings::StepRegistration const BDG_BINDINGS_CAT(bdgStepReg_, __LINE__) \
        BDG_BINDINGS_EARLY(since, &BDG_BINDINGS_CAT(bdgStep_, __LINE__));                               \
    static void BDG_BINDINGS_CAT(bdgStep_, __LINE__)([[maybe_unused]] ::bdg::bindings::Registry & r)
// clang-format on
