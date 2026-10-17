#pragma once
// A call into the game that may fault: guard() runs it under SEH (Windows) or the fault handler (Linux) and answers
// whether it returned. The lambda may hold C++ objects; the frame that guards (guarded) holds none, as a fault unwinds
// past it without running destructors.

#include <cstdint>
#include <type_traits>

namespace bdg::memory {

using GuardedFn = void (*)(void* context);
bool guarded(GuardedFn fn, void* context);

struct Fault {
    unsigned long  code    = 0;  // the exception code (Windows), the signal (Linux)
    std::uintptr_t rva     = 0;  // an absolute address when !inImage
    bool           inImage = false;
};

// the last fault guard() caught on this thread
Fault lastFault();

// Linux: installs the fault handler over the server's own (chained to it), on the thread that is about to read; again
// before each attempt, should the server have installed its own since. Nothing on Windows.
void armFaultHandler();

template <class F>
bool guard(F&& f) {
    return guarded([](void* p) { (*static_cast<std::remove_reference_t<F>*>(p))(); }, &f);
}

}
