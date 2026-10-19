#pragma once
// For game/*.cpp that call Item virtuals no binding names: what tells a slot's function by its code, and a stack of
// one item to call them with.

#include "memory/Mem.h"

#include <cstdint>
#include <cstring>

namespace bdg::game {

// A stack of one item, without enchantments or user data (1.16 - 1.26: the WeakPtr<Item> at +8, a pointer to the Item
// pointer; the Block* at +0x18, a block item's state; the count at +0x22, bool mValid at +0x23). Every use checks
// answers the game is known to give.
class alignas(16) StackOfOne {
public:
    void const* of(std::uintptr_t item, std::uintptr_t block = 0) {
        std::memset(bytes, 0, sizeof bytes);
        holder[0]                   = item;
        std::uintptr_t const target = reinterpret_cast<std::uintptr_t>(&holder[0]);
        std::memcpy(bytes + 8, &target, 8);
        std::memcpy(bytes + 0x18, &block, 8);
        bytes[0x22] = 1;
        bytes[0x23] = 1;
        return bytes;
    }

private:
    std::uint8_t   bytes[0x400]{};
    std::uintptr_t holder[2]{};  // what the WeakPtr points at: the Item*; two words, so the class needs no padding
};

inline bool code(std::uintptr_t fn, std::uint8_t* out, std::size_t n) { return fn && memory::safeRead(reinterpret_cast<void const*>(fn), out, n); }

// `xor al, al` / `xor eax, eax`, then ret
inline bool returnsZero(std::uintptr_t fn) {
    std::uint8_t b[3];
    return code(fn, b, 3) && (b[0] == 0x30 || b[0] == 0x31 || b[0] == 0x32 || b[0] == 0x33) && b[1] == 0xC0 && b[2] == 0xC3;
}

// `movss xmm0, [rip + x]; ret` with 1.0f at x, or `mov eax, 3F800000h; movd xmm0, eax; ret`
inline bool returnsOneFloat(std::uintptr_t fn) {
    std::uint8_t b[10];
    if (!code(fn, b, 10)) {
        return false;
    }
    if (b[0] == 0xF3 && b[1] == 0x0F && b[2] == 0x10 && b[3] == 0x05 && b[8] == 0xC3) {
        std::int32_t rel;
        std::memcpy(&rel, b + 4, 4);
        return memory::read<float>(fn + 8 + rel) == 1.0f;
    }
    std::uint8_t const movd[] = {0xB8, 0x00, 0x00, 0x80, 0x3F, 0x66, 0x0F, 0x6E, 0xC0, 0xC3};
    return std::memcmp(b, movd, sizeof movd) == 0;
}

// `mov eax, 16; ret`
inline bool returnsSixteen(std::uintptr_t fn) {
    std::uint8_t b[6];
    return code(fn, b, 6) && b[0] == 0xB8 && b[1] == 0x10 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 0xC3;
}

}
