#include "stdafx.h"
#include "common.hpp"
#include "feature.hpp"

namespace
{
    // This is an error the original game already expects when the ad servers
    // can't be reached, so it follows the path the original developers made for it
    constexpr int MMT_NETWORK_ERROR = 11;

    int __cdecl NoAdClient(const char*, unsigned int)
    {
        return MMT_NETWORK_ERROR;
    }
}

FEATURE(Engine, MassiveAds)
{
    if (!Memory::WriteIAT(GetModuleHandleW(L"Engine"), "m4d.dll", "MMT_Initialize", NoAdClient))
    {
        spdlog::error("MassiveAds: MMT_Initialize import not found");
        return;
    }

    spdlog::info("MassiveAds: Massive ad client disabled");
}
