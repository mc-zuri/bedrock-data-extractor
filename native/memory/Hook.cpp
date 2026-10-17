#include "memory/Hook.h"

#include <Zydis/Zydis.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace bdg::memory {

namespace {

using Code = std::vector<std::uint8_t>;

constexpr std::size_t JumpSize = 5;  // jmp rel32: what is put over the target's first instructions
constexpr std::size_t PageSize = 4096;

// A page within a rel32 of `function` (the hooked one), read-write until protect(): the detour's relay and the
// trampoline, whose moved instructions keep their rip-relative operands as 32-bit displacements.
std::uint8_t* pageNear(std::uintptr_t function) {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    std::uintptr_t const step = info.dwAllocationGranularity;
#else
    std::uintptr_t const step = 0x10000;
#endif
    std::uintptr_t const base = function & ~(step - 1);
    for (std::uintptr_t delta = step; delta < 0x7FF00000 - step; delta += step) {
        for (std::uintptr_t const at : {base - delta, base + delta}) {
            if (at < 0x10000 || at >= (std::uintptr_t{1} << 47)) {
                continue;
            }
#ifdef _WIN32
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), PageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
                return static_cast<std::uint8_t*>(p);
            }
#else
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
            // a kernel older than 4.17 takes the address as a hint only: where it put the page elsewhere, it is given back
            void* p = mmap(reinterpret_cast<void*>(at), PageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
            if (p == MAP_FAILED) {
                continue;
            }
            if (reinterpret_cast<std::uintptr_t>(p) == at) {
                return static_cast<std::uint8_t*>(p);
            }
            munmap(p, PageSize);
#endif
        }
    }
    return nullptr;
}

void protect(std::uint8_t* page) {
#ifdef _WIN32
    DWORD old = 0;
    VirtualProtect(page, PageSize, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), page, PageSize);
#else
    mprotect(page, PageSize, PROT_READ | PROT_EXEC);
#endif
}

std::optional<std::int32_t> rel32(std::uintptr_t from, std::uintptr_t to) {
    std::int64_t const d = static_cast<std::int64_t>(to - from);
    if (d < INT32_MIN || d > INT32_MAX) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(d);
}

void put32(Code& code, std::size_t at, std::int32_t value) { std::memcpy(code.data() + at, &value, 4); }

void put64(Code& code, std::uint64_t value) {
    std::uint8_t bytes[8];
    std::memcpy(bytes, &value, 8);
    code.insert(code.end(), bytes, bytes + 8);
}

// jmp [rip+0]; the address
void jumpAbsolute(Code& code, std::uintptr_t to) {
    code.insert(code.end(), {0xFF, 0x25, 0, 0, 0, 0});
    put64(code, to);
}

// The instructions of at least JumpSize bytes from `target`, as they run from `at` (the trampoline), then a jump to
// the instruction after them; their length in `taken`. None where one cannot run elsewhere: a short branch whose
// condition has no inverse (jrcxz, loop), a branch into the bytes taken, a function shorter than the jump.
std::optional<Code> relocate(std::uintptr_t target, std::uintptr_t at, std::size_t& taken) {
    ZydisDecoder decoder;
    if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64))) {
        return std::nullopt;
    }
    Code code;
    taken = 0;
    while (taken < JumpSize) {
        std::uintptr_t const    ip = target + taken;
        ZydisDecodedInstruction insn;
        ZydisDecodedOperand     operands[ZYDIS_MAX_OPERAND_COUNT];
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, reinterpret_cast<void const*>(ip), ZYDIS_MAX_INSTRUCTION_LENGTH, &insn, operands))) {
            return std::nullopt;
        }
        std::uintptr_t const next = ip + insn.length;
        std::size_t const    from = code.size();
        bool const           ends = insn.meta.category == ZYDIS_CATEGORY_RET || insn.meta.category == ZYDIS_CATEGORY_UNCOND_BR;
        taken += insn.length;
        if (ends && taken < JumpSize) {
            return std::nullopt;
        }
        if (!(insn.attributes & ZYDIS_ATTRIB_IS_RELATIVE)) {
            code.insert(code.end(), reinterpret_cast<std::uint8_t const*>(ip), reinterpret_cast<std::uint8_t const*>(next));
            continue;
        }
        if (insn.raw.imm[0].is_relative) {
            std::uintptr_t const to = next + static_cast<std::uintptr_t>(insn.raw.imm[0].value.s);
            if (to > target && to < target + JumpSize) {
                return std::nullopt;
            }
            switch (insn.meta.category) {
            case ZYDIS_CATEGORY_UNCOND_BR:
                jumpAbsolute(code, to);
                break;
            case ZYDIS_CATEGORY_CALL:
                // call [rip+2]; jmp over the address; the address
                code.insert(code.end(), {0xFF, 0x15, 2, 0, 0, 0, 0xEB, 8});
                put64(code, to);
                break;
            case ZYDIS_CATEGORY_COND_BR:
                if (insn.mnemonic == ZYDIS_MNEMONIC_JRCXZ || insn.mnemonic == ZYDIS_MNEMONIC_JECXZ || insn.mnemonic == ZYDIS_MNEMONIC_LOOP ||
                    insn.mnemonic == ZYDIS_MNEMONIC_LOOPE || insn.mnemonic == ZYDIS_MNEMONIC_LOOPNE) {
                    return std::nullopt;
                }
                // the inverse condition jumps over the jump taken where the original's holds
                code.insert(code.end(), {static_cast<std::uint8_t>(0x70 | ((insn.opcode & 0xF) ^ 1)), 14});
                jumpAbsolute(code, to);
                break;
            default:
                return std::nullopt;
            }
            continue;
        }
        // a rip-relative memory operand: the same address from where the instruction now is
        if (insn.raw.disp.size != 32) {
            return std::nullopt;
        }
        std::uintptr_t const to = next + static_cast<std::uintptr_t>(insn.raw.disp.value);
        code.insert(code.end(), reinterpret_cast<std::uint8_t const*>(ip), reinterpret_cast<std::uint8_t const*>(next));
        auto const disp = rel32(at + code.size(), to);
        if (!disp) {
            return std::nullopt;
        }
        put32(code, from + insn.raw.disp.offset, *disp);
    }
    auto const back = rel32(at + code.size() + JumpSize, target + taken);
    if (!back) {
        return std::nullopt;
    }
    code.push_back(0xE9);
    code.resize(code.size() + 4);
    put32(code, code.size() - 4, *back);
    return code;
}

// Writes `bytes` (JumpSize of them) over code another thread may be running: in one store where they fit in an aligned
// word, as function starts mostly do, so a thread sees the old instructions or the jump, never half of each.
bool writeCode(std::uintptr_t at, std::uint8_t const* bytes) {
    std::uintptr_t const page = at & ~(PageSize - 1), end = (at + JumpSize + PageSize - 1) & ~(PageSize - 1);
#ifdef _WIN32
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(page), end - page, PAGE_EXECUTE_READWRITE, &old)) {
        return false;
    }
#else
    if (mprotect(reinterpret_cast<void*>(page), end - page, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return false;
    }
#endif
    std::uintptr_t const word = at & ~std::uintptr_t{7};
    if (at + JumpSize <= word + 8) {
        std::uint64_t value = 0;
        std::memcpy(&value, reinterpret_cast<void const*>(word), 8);
        std::memcpy(reinterpret_cast<std::uint8_t*>(&value) + (at - word), bytes, JumpSize);
        std::atomic_ref<std::uint64_t>(*reinterpret_cast<std::uint64_t*>(word)).store(value);
    } else {
        std::memcpy(reinterpret_cast<void*>(at), bytes, JumpSize);
    }
#ifdef _WIN32
    VirtualProtect(reinterpret_cast<void*>(page), end - page, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(at), JumpSize);
#else
    // the server's code is read and executed only
    mprotect(reinterpret_cast<void*>(page), end - page, PROT_READ | PROT_EXEC);
#endif
    return true;
}

struct Installed {
    std::uint8_t jump[JumpSize];
    void*        trampoline;
    bool         on;
};

std::mutex gLock;
// by target. Made on first use: the Linux agent hooks from its preload constructor, which may run before this file's
// statics are constructed
std::map<void*, Installed>& installedHooks() {
    static std::map<void*, Installed> hooks;
    return hooks;
}

}

bool hook(void* target, void* detour, void** original) {
    std::lock_guard lock(gLock);
    if (auto it = installedHooks().find(target); it != installedHooks().end()) {
        *original = it->second.trampoline;
        return it->second.on || (it->second.on = writeCode(reinterpret_cast<std::uintptr_t>(target), it->second.jump));
    }
    auto const    at   = reinterpret_cast<std::uintptr_t>(target);
    std::uint8_t* page = pageNear(at);
    if (!page) {
        return false;
    }
    // the page: the detour's relay (the jump at the target is a rel32, the detour anywhere), then the trampoline
    Code relay;
    jumpAbsolute(relay, reinterpret_cast<std::uintptr_t>(detour));
    std::uintptr_t const trampoline = reinterpret_cast<std::uintptr_t>(page) + 16;
    std::size_t          taken      = 0;
    auto const           moved      = relocate(at, trampoline, taken);
    auto const           toRelay    = rel32(at + JumpSize, reinterpret_cast<std::uintptr_t>(page));
    if (!moved || !toRelay || 16 + moved->size() > PageSize) {
#ifdef _WIN32
        VirtualFree(page, 0, MEM_RELEASE);
#else
        munmap(page, PageSize);
#endif
        return false;
    }
    std::memcpy(page, relay.data(), relay.size());
    std::memcpy(page + 16, moved->data(), moved->size());
    protect(page);

    Installed installed{};
    installed.jump[0] = 0xE9;
    std::memcpy(installed.jump + 1, &*toRelay, 4);
    installed.trampoline = reinterpret_cast<void*>(trampoline);
    *original            = installed.trampoline;
    installed.on         = writeCode(at, installed.jump);
    installedHooks().emplace(target, installed);
    return installed.on;
}

}
