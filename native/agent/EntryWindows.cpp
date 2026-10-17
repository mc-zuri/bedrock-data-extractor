// Windows: injected into bedrock_server.exe after "Server started" (bdg_inject.exe); DllMain starts the worker that
// resolves the bindings and hooks Level::tick.
#include <windows.h>

#include "agent/Agent.h"
#include "bindings/Names.h"
#include "core/Log.h"
#include "memory/Hook.h"
#include "memory/Scanner.h"

#include <format>
#include <string>
#include <string_view>

namespace {

namespace n = bdg::bindings::names;

using TickFn = void(__fastcall*)(void*);
TickFn gTick = nullptr;

EXCEPTION_RECORD gFault{};

int noteFault(EXCEPTION_POINTERS const* e) {
    gFault = *e->ExceptionRecord;
    return EXCEPTION_EXECUTE_HANDLER;
}

// where: an offset into the module it is in (the agent's, the server's)
void faulted() {
    auto const at             = reinterpret_cast<std::uintptr_t>(gFault.ExceptionAddress);
    HMODULE    module         = nullptr;
    char       name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(at), &module)) {
        GetModuleFileNameA(module, name, MAX_PATH);
    }
    std::string_view const file = std::string_view(name).substr(std::string_view(name).find_last_of('\\') + 1);
    bdg::agent::finish(std::format("FAILED: the export faulted: exception 0x{:X} at {}+0x{:X}", gFault.ExceptionCode, file, at - reinterpret_cast<std::uintptr_t>(module)));
}

// no C++ objects in a function with __try
bool guardedAttempt(std::uintptr_t tickSelf) {
    __try {
        return bdg::agent::attempt(tickSelf);
    } __except (noteFault(GetExceptionInformation())) {
        faulted();
        return true;
    }
}

void __fastcall hookedTick(void* self) {
    gTick(self);
    bdg::agent::onTick(reinterpret_cast<std::uintptr_t>(self), &guardedAttempt);
}

DWORD WINAPI worker(LPVOID) {
    if (!bdg::agent::start()) {
        return 1;
    }
    auto& r = bdg::agent::bindings();
    // before hooking: the hook rewrites the very bytes the Level::tick pattern matches
    auto failed = r.resolve(bdg::memory::Module::game());
    for (auto const& [name, value] : r.describe()) {
        bdg::log::info("  {} = {}", name, value);
    }
    if (!failed.empty()) {
        std::string list;
        for (auto const& f : failed) {
            list += (list.empty() ? "" : ", ") + f;
        }
        bdg::agent::finish("FAILED: unresolved bindings: " + list);
        return 1;
    }
    std::uintptr_t tick = r.address(n::LevelTick);
    if (!tick) {
        bdg::agent::finish("FAILED: no Level::tick binding");
        return 1;
    }
    if (!bdg::memory::hook(reinterpret_cast<void*>(tick), reinterpret_cast<void*>(&hookedTick), reinterpret_cast<void**>(&gTick))) {
        bdg::agent::finish("FAILED: could not hook Level::tick");
        return 1;
    }
    bdg::log::info("hooked Level::tick at 0x{:X}", r.rvaOf(n::LevelTick));
    return 0;
}

}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (HANDLE t = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr)) {
            CloseHandle(t);
        }
    }
    return TRUE;
}
