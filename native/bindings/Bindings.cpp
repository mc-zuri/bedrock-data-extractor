#include "bindings/Bindings.h"

#include "memory/Image.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace bdg::bindings {

namespace {

std::vector<Step>& steps() {
    static std::vector<Step> s;
    return s;
}

Registry& registry() {
    static Registry r;
    return r;
}

}

// A name is bound one way at a time: a later step's pattern replaces an earlier step's symbol, an offset a getter.
void Registry::function(std::string_view name, std::string_view pattern, int rel32At) {
    drop(name);
    mFunctions.insert_or_assign(std::string(name), Function{std::string(pattern), rel32At, currentStep, 0});
}

void Registry::symbol(std::string_view name, std::string_view mangled) {
    drop(name);
    mSymbols.insert_or_assign(std::string(name), Named{std::string(mangled), "", 0, currentStep, 0});
}

void Registry::slotOf(std::string_view name, std::string_view vtable, std::string_view function) {
    drop(name);
    mSlotsOf.insert_or_assign(std::string(name), Named{std::string(vtable), std::string(function), 0, currentStep, 0});
}

void Registry::offsetOf(std::string_view name, std::string_view getter, std::int64_t adjust) {
    drop(name);
    mOffsetsOf.insert_or_assign(std::string(name), Named{std::string(getter), "", adjust, currentStep, 0});
}

void Registry::offsetIn(std::string_view name, std::string_view function, std::string_view prefix) {
    drop(name);
    mOffsetsIn.insert_or_assign(std::string(name), Named{std::string(function), std::string(prefix), 0, currentStep, 0});
}

void Registry::drop(std::string_view name) {
    if (auto it = mOffsetsIn.find(name); it != mOffsetsIn.end()) {
        mOffsetsIn.erase(it);
    }
    if (auto it = mFunctions.find(name); it != mFunctions.end()) {
        mFunctions.erase(it);
    }
    if (auto it = mSymbols.find(name); it != mSymbols.end()) {
        mSymbols.erase(it);
    }
    if (auto it = mSlotsOf.find(name); it != mSlotsOf.end()) {
        mSlotsOf.erase(it);
    }
    if (auto it = mOffsetsOf.find(name); it != mOffsetsOf.end()) {
        mOffsetsOf.erase(it);
    }
    if (auto it = mSlots.find(name); it != mSlots.end()) {
        mSlots.erase(it);
    }
    if (auto it = mOffsets.find(name); it != mOffsets.end()) {
        mOffsets.erase(it);
    }
    if (auto it = mVariants.find(name); it != mVariants.end()) {
        mVariants.erase(it);
    }
}

void Registry::slot(std::string_view name, int index) {
    drop(name);
    mSlots.insert_or_assign(std::string(name), index);
}

void Registry::offset(std::string_view name, std::int64_t value) {
    drop(name);
    mOffsets.insert_or_assign(std::string(name), value);
}

void Registry::variant(std::string_view name, int value) { mVariants.insert_or_assign(std::string(name), value); }

void Registry::foundOffset(std::string_view name, std::int64_t value) {
    drop(name);
    mOffsets.insert_or_assign(std::string(name), value);
    mFound.insert(std::string(name));
}

void Registry::foundFunction(std::string_view name, std::uintptr_t address) {
    drop(name);
    mSymbols.insert_or_assign(std::string(name), Named{"(found by content)", "", 0, "", address - memory::imageBase()});
    mFound.insert(std::string(name));
}

std::vector<std::string> Registry::resolve(memory::Module const& module) {
    std::vector<std::string> failed;
    for (auto& [name, fn] : mFunctions) {
        fn.resolved  = 0;
        auto pattern = memory::Pattern::parse(fn.pattern);
        if (!pattern || (fn.rel32At >= 0 && static_cast<std::size_t>(fn.rel32At) + 4 > pattern->size())) {
            failed.push_back(name + " (bad pattern)");
            continue;
        }
        auto hits = pattern->find(module.textBegin, module.textEnd, 2);
        if (hits.size() != 1) {
            failed.push_back(std::format("{} ({} matches, step {})", name, hits.size(), fn.step));
            continue;
        }
        std::uintptr_t at = hits[0];
        if (fn.rel32At < 0) {
            fn.resolved = module.rva(at);
        } else {
            std::int32_t disp = 0;
            std::memcpy(&disp, reinterpret_cast<void const*>(at + fn.rel32At), 4);
            fn.resolved = static_cast<std::uint32_t>(static_cast<std::int64_t>(module.rva(at)) + fn.rel32At + 4 + disp);
        }
    }
    return failed;
}

namespace {

std::int64_t displacement(std::uint8_t const* code, std::size_t at, bool wide) {
    if (!wide) {
        return static_cast<std::int8_t>(code[at]);
    }
    std::int32_t d;
    std::memcpy(&d, code + at, 4);
    return d;
}

// The member offset a getter reads from `this`, where reading it is all the getter does: one instruction, then ret.
//   (rdi = this) 48 8D lea rax / 48 8B mov rax / 8B mov eax / 0F B6 movzx eax, byte / 0F B7 movzx eax, word /
//   F3 0F 10 movss xmm0, from [rdi] (modrm 07), [rdi + disp8] (47) or [rdi + disp32] (87); then C3
//   a small struct returned through a hidden pointer (Brightness: rdi = &ret, rsi = this), the byte at [rsi + disp] put
//   there: `mov rax, rdi; movzx ecx, byte [rsi + d]; mov [rdi], cl; ret` (clang 6 with libc++) or `mov al, [rsi + d];
//   mov [rdi], al; mov rax, rdi; ret` (with libstdc++)
//   the member's address computed in rdi and returned: `add rdi, d` or `sub rdi, -d`, then `mov rax, rdi; ret`
// -1 for any other code: a getter that does more (reads through a pointer, scales the value) is not guessed at.
std::int64_t optimizedGetterOffset(std::uint8_t const* code) {
    // mov rax, rdi; movzx ecx, byte [rsi + d]; mov [rdi], cl; ret
    if (code[0] == 0x48 && code[1] == 0x89 && code[2] == 0xF8 && code[3] == 0x0F && code[4] == 0xB6 && (code[5] == 0x4E || code[5] == 0x8E)) {
        bool const        wide = code[5] == 0x8E;
        std::size_t const next = 6 + (wide ? 4 : 1);
        return code[next] == 0x88 && code[next + 1] == 0x0F && code[next + 2] == 0xC3 ? displacement(code, 6, wide) : -1;
    }
    // mov al, [rsi + d]; mov [rdi], al; mov rax, rdi; ret
    if (code[0] == 0x8A && (code[1] == 0x46 || code[1] == 0x86)) {
        bool const         wide   = code[1] == 0x86;
        std::size_t const  next   = 2 + (wide ? 4 : 1);
        std::uint8_t const tail[] = {0x88, 0x07, 0x48, 0x89, 0xF8, 0xC3};
        return std::memcmp(code + next, tail, sizeof tail) == 0 ? displacement(code, 2, wide) : -1;
    }
    // the member's address: add rdi, d (or sub rdi, -d: clang's imm8 for 128); mov rax, rdi; ret
    if (code[0] == 0x48 && (code[1] == 0x83 || code[1] == 0x81) && (code[2] == 0xC7 || code[2] == 0xEF)) {
        bool const         wide   = code[1] == 0x81;
        std::uint8_t const tail[] = {0x48, 0x89, 0xF8, 0xC3};
        std::int64_t const d      = displacement(code, 3, wide);
        if (std::memcmp(code + 3 + (wide ? 4 : 1), tail, sizeof tail) != 0) {
            return -1;
        }
        return code[2] == 0xEF ? -d : d;
    }
    std::size_t op;
    if (code[0] == 0x48 && (code[1] == 0x8D || code[1] == 0x8B)) {
        op = 2;
    } else if (code[0] == 0x8B) {
        op = 1;
    } else if (code[0] == 0x0F && (code[1] == 0xB6 || code[1] == 0xB7)) {
        op = 2;
    } else if (code[0] == 0xF3 && code[1] == 0x0F && code[2] == 0x10) {
        op = 3;
    } else {
        return -1;
    }
    switch (code[op]) {
    case 0x07:
        return code[op + 1] == 0xC3 ? 0 : -1;
    case 0x47:
        return code[op + 2] == 0xC3 ? displacement(code, op + 1, false) : -1;
    case 0x87:
        return code[op + 5] == 0xC3 ? displacement(code, op + 1, true) : -1;
    default:
        return -1;
    }
}

// The same getter built without optimization (1.16.201 - 1.16.210): `push rbp; mov rbp, rsp` (`sub rsp, n`), the
// arguments spilled to the frame (`this` to [rbp - 8]; rdi kept in rax first where it is the hidden pointer and `this`
// is rsi), `this` loaded back into a register, then the member: its address (`add reg, d`, handed to a copy constructor
// or unique_ptr::get) or its value (a load from [reg + d]); what follows is the epilogue, a spill, a call or a move of
// the address on, never a read through what was loaded.
std::int64_t unoptimizedGetterOffset(std::uint8_t const* code, std::size_t size) {
    std::uint8_t const prologue[] = {0x55, 0x48, 0x89, 0xE5};
    if (size < 40 || std::memcmp(code, prologue, sizeof prologue) != 0) {
        return -1;
    }
    std::size_t at = 4;
    if (code[at] == 0x48 && code[at + 1] == 0x83 && code[at + 2] == 0xEC) {
        at += 4;
    }
    // mov rax, rdi; mov [rbp + d8], reg
    for (int spills = 0; spills < 4; ++spills) {
        if (code[at] == 0x48 && code[at + 1] == 0x89 && code[at + 2] == 0xF8) {
            at += 3;
        } else if (code[at] == 0x48 && code[at + 1] == 0x89 && (code[at + 2] & 0xC7) == 0x45) {
            at += 4;
        } else {
            break;
        }
    }
    // mov reg, [rbp - 8]
    if (code[at] != 0x48 || code[at + 1] != 0x8B || (code[at + 2] & 0xC7) != 0x45 || code[at + 3] != 0xF8) {
        return -1;
    }
    int const self = (code[at + 2] >> 3) & 7;
    at += 4;
    std::int64_t off = -1;
    std::size_t  next;
    if (code[at] == 0x48 && (code[at + 1] == 0x83 || code[at + 1] == 0x81) && code[at + 2] == (0xC0 | self)) {
        bool const wide = code[at + 1] == 0x81;
        off             = displacement(code, at + 3, wide);
        next            = at + 3 + (wide ? 4 : 1);
    } else {
        std::size_t op;
        if (code[at] == 0x48 && (code[at + 1] == 0x8D || code[at + 1] == 0x8B)) {
            op = 2;
        } else if (code[at] == 0x8B || code[at] == 0x8A) {
            op = 1;
        } else if (code[at] == 0x0F && (code[at + 1] == 0xB6 || code[at + 1] == 0xB7)) {
            op = 2;
        } else if (code[at] == 0xF3 && code[at + 1] == 0x0F && code[at + 2] == 0x10) {
            op = 3;
        } else {
            return -1;
        }
        std::uint8_t const modrm = code[at + op];
        int const          mod   = modrm >> 6;
        if ((modrm & 7) != self || mod == 3) {
            return -1;
        }
        off  = mod == 0 ? 0 : displacement(code, at + op + 1, mod == 2);
        next = at + op + 1 + (mod == 0 ? 0 : mod == 1 ? 1 : 4);
    }
    std::uint8_t const* n        = code + next;
    bool const          handedOn = n[0] == 0x5D || n[0] == 0xE8 || (n[0] == 0x48 && n[1] == 0x89 && (n[2] == 0xF8 || n[2] == 0xD6 || (n[2] & 0xC7) == 0x45));
    return handedOn ? off : -1;
}

std::int64_t getterOffset(std::uint8_t const* code, std::size_t size) {
    std::int64_t const off = optimizedGetterOffset(code);
    return off >= 0 ? off : unoptimizedGetterOffset(code, size);
}

}

std::vector<std::string> Registry::resolveNames(Symbols const& symbols) {
    std::vector<std::string> wanted, failed;
    for (auto const* list : {&mSymbols, &mSlotsOf, &mOffsetsOf, &mOffsetsIn}) {
        for (auto const& [name, n] : *list) {
            wanted.push_back(n.symbol);
            if (list == &mSlotsOf) {
                wanted.push_back(n.function);
            }
        }
    }
    auto const at = symbols.addresses(wanted);
    auto       of = [&](std::string const& mangled) -> std::uint64_t {
        auto it = at.find(mangled);
        return it == at.end() ? 0 : it->second;
    };
    for (auto& [name, n] : mSymbols) {
        if (!(n.resolved = of(n.symbol))) {
            failed.push_back(std::format("{} (no symbol {}, step {})", name, n.symbol, n.step));
        }
    }
    for (auto& [name, n] : mSlotsOf) {
        n.resolved                 = 0;
        std::uint64_t const vtable = of(n.symbol), fn = of(n.function);
        // an Itanium vtable: offset to top, typeinfo, then the slots
        for (int slot = 0; vtable && fn && slot < 2000; ++slot) {
            if (symbols.word(vtable + 16 + slot * 8) == fn) {
                n.resolved = static_cast<std::uint64_t>(slot) + 1;  // 0 is "unresolved"
                break;
            }
        }
        if (!n.resolved) {
            failed.push_back(std::format("{} ({} not in {}, step {})", name, n.function, n.symbol, n.step));
        }
    }
    for (auto& [name, n] : mOffsetsOf) {
        n.resolved                   = 0;
        std::uint8_t        code[48] = {};
        std::uint64_t const fn       = of(n.symbol);
        std::int64_t const  off      = fn && symbols.bytes(fn, code, sizeof code) ? getterOffset(code, sizeof code) : -1;
        if (off >= 0) {
            n.resolved = static_cast<std::uint64_t>(off + n.adjust) + 1;
        } else {
            failed.push_back(std::format("{} (getter {} {}, step {})", name, n.symbol, fn ? "reads no member" : "has no symbol", n.step));
        }
    }
    for (auto& [name, n] : mOffsetsIn) {
        n.resolved                   = 0;
        auto const          prefix   = memory::Pattern::parse(n.function);
        std::uint8_t        code[64] = {};
        std::uint64_t const fn       = of(n.symbol);
        if (prefix && fn && symbols.bytes(fn, code, sizeof code)) {
            auto const at   = reinterpret_cast<std::uintptr_t>(code);
            auto const hits = prefix->find(at, at + sizeof code - 4, 1);
            if (!hits.empty()) {
                std::int32_t disp;
                std::memcpy(&disp, reinterpret_cast<void const*>(hits[0] + prefix->size()), 4);
                n.resolved = static_cast<std::uint64_t>(disp) + 1;
            }
        }
        if (!n.resolved) {
            failed.push_back(std::format("{} ({} has no {} in its first 64 bytes, step {})", name, n.symbol, n.function, n.step));
        }
    }
    return failed;
}

bool Registry::has(std::string_view name) const { return rvaOf(name) != 0; }

std::uint64_t Registry::rvaOf(std::string_view name) const {
    if (auto it = mSymbols.find(name); it != mSymbols.end()) {
        return it->second.resolved;
    }
    auto it = mFunctions.find(name);
    return it == mFunctions.end() ? 0 : it->second.resolved;
}

std::uintptr_t Registry::address(std::string_view name) const {
    std::uint64_t rva = rvaOf(name);
    return rva ? memory::imageBase() + rva : 0;
}

int Registry::slot(std::string_view name) const {
    if (auto it = mSlotsOf.find(name); it != mSlotsOf.end()) {
        return it->second.resolved ? static_cast<int>(it->second.resolved - 1) : -1;
    }
    auto it = mSlots.find(name);
    return it == mSlots.end() ? -1 : it->second;
}

std::int64_t Registry::offset(std::string_view name, std::int64_t fallback) const {
    if (auto it = mOffsetsOf.find(name); it != mOffsetsOf.end()) {
        return it->second.resolved ? static_cast<std::int64_t>(it->second.resolved - 1) : fallback;
    }
    if (auto it = mOffsetsIn.find(name); it != mOffsetsIn.end()) {
        return it->second.resolved ? static_cast<std::int64_t>(it->second.resolved - 1) : fallback;
    }
    auto it = mOffsets.find(name);
    return it == mOffsets.end() ? fallback : it->second;
}

int Registry::variant(std::string_view name, int fallback) const {
    auto it = mVariants.find(name);
    return it == mVariants.end() ? fallback : it->second;
}

std::map<std::string, std::string> Registry::describe() const {
    std::map<std::string, std::string> out;
    for (auto const& [name, fn] : mFunctions) {
        out[name] = fn.resolved ? std::format("0x{:X} (step {})", fn.resolved, fn.step) : std::format("unresolved (step {})", fn.step);
    }
    for (auto const& [name, v] : mSlots) {
        out[name] = std::format("slot {}", v);
    }
    for (auto const& [name, v] : mOffsets) {
        out[name] = std::format("0x{:X}{}", v, mFound.contains(name) ? " (found by content)" : "");
    }
    for (auto const& [name, v] : mVariants) {
        out[name] = std::format("variant {}", v);
    }
    for (auto const& [name, n] : mSymbols) {
        out[name] = n.resolved ? std::format("0x{:X} = {} (step {})", n.resolved, n.symbol, n.step) : std::format("unresolved {} (step {})", n.symbol, n.step);
    }
    for (auto const& [name, n] : mSlotsOf) {
        out[name] = n.resolved ? std::format("slot {} of {} (step {})", n.resolved - 1, n.symbol, n.step) : std::format("unresolved slot (step {})", n.step);
    }
    for (auto const& [name, n] : mOffsetsOf) {
        out[name] = n.resolved ? std::format("0x{:X} from {} (step {})", n.resolved - 1, n.symbol, n.step) : std::format("unresolved offset (step {})", n.step);
    }
    for (auto const& [name, n] : mOffsetsIn) {
        out[name] = n.resolved ? std::format("0x{:X} in {} (step {})", n.resolved - 1, n.symbol, n.step) : std::format("unresolved offset (step {})", n.step);
    }
    return out;
}

int addStep(char const* since, StepFn fn) {
    steps().push_back({Version::parse(since), since, fn});
    return 0;
}

Registry& load(Version running) {
    auto ordered = steps();
    std::ranges::stable_sort(ordered, {}, &Step::since);
    Registry& r = registry();
    for (auto const& step : ordered) {
        if (step.since > running) {
            break;
        }
        r.currentStep = step.name;
        step.apply(r);
    }
    r.currentStep.clear();
    return r;
}

Registry& current() { return registry(); }

}
