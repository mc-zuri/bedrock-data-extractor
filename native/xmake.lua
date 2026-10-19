add_rules("mode.release", "mode.debug")
set_defaultmode("release")
set_languages("c++20")

-- Windows (MSVC x64): the agent injected into bedrock_server.exe, bound by patterns (bindings/Steps.cpp), guarded by SEH.
-- Linux (clang or gcc, x86-64): the agent preloaded into bedrock_server, bound by the server's own symbols where it has
-- them (bindings/StepsLinux.cpp), guarded by a fault handler.
if is_plat("windows") then
    set_runtimes("MD")
    add_requires("zydis 4.1.1")

    -- what every binary shares: the bindings (an object library, so the static step registrations are linked) and the pattern scanner
    target("bdg_bindings")
        set_kind("object")
        add_files("core/*.cpp", "memory/Guard.cpp", "memory/Image.cpp", "memory/Scanner.cpp", "memory/Rtti.cpp", "bindings/Bindings.cpp", "bindings/Steps.cpp")
        add_includedirs(".", {public = true})
        add_cxflags("/EHsc", "/utf-8", "/W4", "/permissive-")
        add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS", {public = true})

    -- injected into bedrock_server.exe
    target("bdg_agent")
        set_kind("shared")
        add_deps("bdg_bindings")
        add_packages("zydis")
        add_files("game/*.cpp", "export/*.cpp", "agent/Agent.cpp", "agent/EntryWindows.cpp", "memory/Hook.cpp", "memory/Flow.cpp", "memory/Code.cpp")
        -- SEH-guarded foreign calls sit next to C++ objects
        set_exceptions("none")
        add_cxflags("/EHa", "/utf-8", "/W4", "/permissive-")

    target("bdg_inject")
        set_kind("binary")
        add_files("inject/*.cpp")
        add_cxflags("/EHsc", "/utf-8", "/W4", "/permissive-")
        add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")

    -- resolves the bindings against a server exe on disk, without starting it
    target("bdg_check")
        set_kind("binary")
        add_deps("bdg_bindings")
        add_files("check/Check.cpp")
        add_cxflags("/EHsc", "/utf-8", "/W4", "/permissive-")
elseif is_plat("linux") then
    add_requires("zydis 4.1.1")

    target("bdg_bindings")
        set_kind("object")
        add_files("core/*.cpp", "memory/Guard.cpp", "memory/Image.cpp", "memory/Scanner.cpp", "memory/Elf.cpp", "memory/Rtti.cpp", "bindings/Bindings.cpp", "bindings/StepsLinux.cpp")
        add_includedirs(".", {public = true})
        add_cxflags("-fPIC", "-Wall", "-Wextra", {public = true})

    -- preloaded into bedrock_server (LD_PRELOAD); its own C++ runtime linked in, the server's left alone
    target("bdg_agent")
        set_kind("shared")
        add_deps("bdg_bindings")
        add_packages("zydis")
        add_files("game/*.cpp", "export/*.cpp", "agent/Agent.cpp", "agent/EntryLinux.cpp", "memory/Hook.cpp", "memory/Flow.cpp", "memory/Code.cpp")
        -- a shared library's link flags are shflags; their symbols stay its own, so the server's older system
        -- libstdc++ (Debian 12's) is never asked for
        add_shflags("-static-libstdc++", "-static-libgcc", "-Wl,--exclude-libs,ALL")
        add_syslinks("dl", "pthread")

    -- resolves the name bindings against a server on disk, without starting it
    target("bdg_check")
        set_kind("binary")
        add_deps("bdg_bindings")
        add_files("check/CheckLinux.cpp")
end
