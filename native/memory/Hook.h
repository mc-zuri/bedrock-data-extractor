#pragma once
// An inline hook on a game function: its first instructions moved to a trampoline, a jump to the detour put in their
// place. The instructions are decoded with Zydis.

namespace bdg::memory {

// Hooks `target`; `*original` is then what calls the function as it was. False when it cannot be hooked.
bool hook(void* target, void* detour, void** original);

}
