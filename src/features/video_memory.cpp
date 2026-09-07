#include "stdafx.h"
#include "common.hpp"
#include "feature.hpp"

namespace
{
    // WMI reports roughly 4095 MB for modern cards, but the signed conversion makes 
    // values over 2 GB negative, causing the -5 MB in the log
    constexpr ptrdiff_t FirstShift  = 10;
    constexpr ptrdiff_t SecondShift = 22;
    constexpr uint8_t   ShrEax      = 0xE8;     // C1 /7 SAR EAX -> C1 /5 SHR EAX
}

FEATURE(Startup, VideoMemory)
{
    // CDQ / AND EDX,3FF / ADD EAX,EDX / SAR EAX,0A, twice
    auto toMegabytes = hook::module_pattern(GetModuleHandleW(nullptr),
        "99 81 E2 FF 03 00 00 03 C2 C1 F8 0A 99 81 E2 FF 03 00 00 03 C2 C1 F8 0A");

    if (toMegabytes.empty())
    {
        spdlog::error("VideoMemory: pattern not found (toMegabytes)");
        return;
    }

    injector::WriteMemory<uint8_t>(toMegabytes.get_first(FirstShift), ShrEax, true);
    injector::WriteMemory<uint8_t>(toMegabytes.get_first(SecondShift), ShrEax, true);

    if (GetModuleHandleW(L"WinDrv"))
        spdlog::warn("VideoMemory: patched {} after the min spec check already ran, needs the winmm.dll loader",
                     toMegabytes.get_first(0));
    else
        spdlog::info("VideoMemory: onboard video memory measured unsigned at {}", toMegabytes.get_first(0));
}
