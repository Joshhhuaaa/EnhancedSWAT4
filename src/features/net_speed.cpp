#include "stdafx.h"
#include "common.hpp"
#include "feature.hpp"
#include "script.hpp"

// The Game Controls page has four Connection Speed presets from
// SwatGUIConfig.NetworkConnectionSpeeds in SwatGUIState.ini:
// Modem      2600
// ISDN       5000
// Cable/ADSL 10000
// LAN/T1     15000

// The engine floors the rate at 1800 but never ceilings it. Stock servers cap 
// clients at MaxClientRate (20000), so higher rates work on raised limits.
static constexpr bool    bForceNetSpeed = true;
static constexpr int32_t iNetSpeed      = 480000;

namespace
{
    SafetyHookInline shUNetConnection{};

    void* __fastcall UNetConnection(void* self, void*, void* driver, void* url)
    {
        if (auto speed = static_cast<int32_t*>(Script::Default(L"Player", L"ConfiguredInternetSpeed")))
            *speed = iNetSpeed;
        else
            spdlog::error("ForceNetSpeed: Engine.Player.ConfiguredInternetSpeed not found");

        return shUNetConnection.thiscall<void*>(self, driver, url);
    }
}

FEATURE(Engine, ForceNetSpeed)
{
    if (!bForceNetSpeed)
        return;

    auto netConnection = GetProcAddress(GetModuleHandleW(L"Engine"), "??0UNetConnection@@QAE@PAVUNetDriver@@ABVFURL@@@Z");

    if (!netConnection)
    {
        spdlog::error("ForceNetSpeed: UNetConnection constructor not found");
        return;
    }

    shUNetConnection = safetyhook::create_inline(netConnection, UNetConnection);
    spdlog::info("ForceNetSpeed: Connection rate forced to {}", iNetSpeed);
}
