#include "stdafx.h"
#include "common.hpp"
#include "feature.hpp"

namespace
{
    // Replacements must remain the same length to preserve the in-place write
    constexpr struct { const char* from; const char* to; } Hosts[] =
    {
        { "%s.available.gamespy.com", "available.swat4stats.com" },
        { "%s.master.gamespy.com",    "master.swat4stats.com"    },
        { "%s.ms%d.gamespy.com",      "ms15.swat4stats.com"      },
    };

    // hook::module_pattern stops at the last executable section, and these live in .rdata
    void* Find(HMODULE module, const char* text, size_t length)
    {
        auto address = reinterpret_cast<uint8_t*>(module);
        auto header  = reinterpret_cast<IMAGE_NT_HEADERS*>(address + reinterpret_cast<IMAGE_DOS_HEADER*>(module)->e_lfanew);

        for (auto end = address + header->OptionalHeader.SizeOfImage - length; address <= end; ++address)
            if (!memcmp(address, text, length))
                return address;

        return nullptr;
    }
}

FEATURE(Engine, MasterServer)
{
    auto engine = GetModuleHandleW(L"Engine");

    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(engine, module, MAX_PATH);
    auto path = std::filesystem::path(module).string();

    for (auto& host : Hosts)
    {
        auto length = strlen(host.from);

        // A pre-patched Engine.dll has nothing left to rewrite
        if (auto address = Find(engine, host.from, length))
            injector::WriteMemoryRaw(address, const_cast<char*>(host.to), length, true);
        else if (!Find(engine, host.to, length))
        {
            spdlog::error("MasterServer: {} not found in {}", host.from, path);
            return;
        }
    }

    spdlog::info("MasterServer: Server browser pointed at swat4stats.com ({})", path);
}
