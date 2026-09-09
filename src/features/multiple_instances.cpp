#include "stdafx.h"
#include "common.hpp"
#include "config.hpp"
#include "feature.hpp"

static Config::Value bAllowMultipleInstances("Debug", "AllowMultipleInstances", false);

namespace
{
    HANDLE WINAPI NoAlreadyExists(LPSECURITY_ATTRIBUTES attributes, BOOL initialOwner, LPCWSTR name)
    {
        auto mutex = CreateMutexW(attributes, initialOwner, name);
        SetLastError(ERROR_SUCCESS);
        return mutex;
    }
}

FEATURE(Startup, AllowMultipleInstances)
{
    if (!bAllowMultipleInstances)
        return;

    // InitEngine checks GetLastError after creating Swat4IsRunning and treats ERROR_ALREADY_EXISTS
    // as another instance of the game. The CreateMutexA branch is for Win9x and is left alone.
    if (!Memory::WriteIAT(GetModuleHandleW(nullptr), "KERNEL32.dll", "CreateMutexW", NoAlreadyExists))
    {
        spdlog::error("AllowMultipleInstances: CreateMutexW import not found");
        return;
    }

    if (GetModuleHandleW(L"WinDrv"))
        spdlog::warn("AllowMultipleInstances: patched after InitEngine already ran, needs the winmm.dll loader");
    else
        spdlog::info("AllowMultipleInstances: Single instance check disabled");
}
