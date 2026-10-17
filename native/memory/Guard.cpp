#include "memory/Guard.h"

#include "memory/Mem.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <ucontext.h>

#include <csetjmp>
#include <csignal>
#include <cstring>
#endif

namespace bdg::memory {

namespace {

thread_local Fault tLast;

void note(unsigned long code, std::uintptr_t at) {
    tLast.code    = code;
    tLast.inImage = inImage(at);
    tLast.rva     = tLast.inImage ? at - imageBase() : at;
}

}

Fault lastFault() { return tLast; }

#ifdef _WIN32

namespace {

int record(EXCEPTION_POINTERS const* info) {
    note(info->ExceptionRecord->ExceptionCode, reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress));
    return EXCEPTION_EXECUTE_HANDLER;
}

}

bool guarded(GuardedFn fn, void* context) {
    __try {
        fn(context);
        return true;
    } __except (record(GetExceptionInformation())) {
        return false;
    }
}

void armFaultHandler() {}

#else

// A read or a call that faults jumps back to where it was guarded. The handler leaves the signals unblocked
// (SA_NODEFER: sigsetjmp(…, 0) does not restore the mask), and hands a fault outside a guard to the handler the
// server had installed, so its own crash handling is as it was.
namespace {

thread_local sigjmp_buf* tJump = nullptr;
struct sigaction         gPrevious[2];

void onFault(int sig, siginfo_t* info, void* uc) {
    if (tJump) {
        // where: the faulting instruction (si_addr is what it touched)
        note(static_cast<unsigned long>(sig), static_cast<std::uintptr_t>(static_cast<ucontext_t*>(uc)->uc_mcontext.gregs[REG_RIP]));
        siglongjmp(*tJump, 1);
    }
    struct sigaction const& prev = gPrevious[sig == SIGBUS ? 1 : 0];
    if (prev.sa_flags & SA_SIGINFO) {
        if (prev.sa_sigaction) {
            prev.sa_sigaction(sig, info, uc);
        }
    } else if (prev.sa_handler != SIG_DFL && prev.sa_handler != SIG_IGN) {
        prev.sa_handler(sig);
    } else {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

}

void armFaultHandler() {
    struct sigaction mine;
    std::memset(&mine, 0, sizeof mine);
    sigemptyset(&mine.sa_mask);
    mine.sa_flags     = SA_SIGINFO | SA_NODEFER;
    mine.sa_sigaction = &onFault;
    struct sigaction current;
    // installed once the server has set its own, and again should it set them after
    if (sigaction(SIGSEGV, nullptr, &current) == 0 && current.sa_sigaction != &onFault) {
        sigaction(SIGSEGV, &mine, &gPrevious[0]);
    }
    if (sigaction(SIGBUS, nullptr, &current) == 0 && current.sa_sigaction != &onFault) {
        sigaction(SIGBUS, &mine, &gPrevious[1]);
    }
}

bool safeRead(void const* addr, void* out, std::size_t len) {
    sigjmp_buf  here;
    sigjmp_buf* outer = tJump;
    tJump             = &here;
    if (sigsetjmp(here, 0)) {
        tJump = outer;
        return false;
    }
    std::memcpy(out, addr, len);
    tJump = outer;
    return true;
}

bool guarded(GuardedFn fn, void* context) {
    sigjmp_buf  here;
    sigjmp_buf* outer = tJump;
    tJump             = &here;
    if (sigsetjmp(here, 0)) {
        tJump = outer;
        return false;
    }
    try {
        fn(context);
    } catch (...) {
        // a C++ exception the game threw, which SEH catches on Windows: recorded with its code there
        note(0xE06D7363, 0);
        tJump = outer;
        return false;
    }
    tJump = outer;
    return true;
}

#endif

}
