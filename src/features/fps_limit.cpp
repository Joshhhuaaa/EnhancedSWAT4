#include "stdafx.h"
#include "common.hpp"
#include "config.hpp"
#include "feature.hpp"

#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

static Config::Value nFPSLimit("General", "FPSLimit", 0);

namespace
{
    SafetyHookInline shGetMaxTickRate{};

    HANDLE  hTimer       = nullptr;
    int64_t qpcFrequency = 0;
    int64_t spinTicks    = 0;

    // Leaves headroom for GPU present jitter without blocking Present or exceeding VRR range
    constexpr int32_t RefreshMargin = 3;

    int32_t RefreshRate()
    {
        static int32_t cached = 0;
        static int64_t next   = 0;

        // Refreshes once per second to catch fullscreen and monitor changes
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (now.QuadPart < next)
            return cached;
        next = now.QuadPart + qpcFrequency;

        if (auto window = GetActiveWindow())
        {
            MONITORINFOEXW monitor{};
            DEVMODEW mode{};
            monitor.cbSize = sizeof(monitor);
            mode.dmSize    = sizeof(mode);

            if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor) &&
                EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
                mode.dmDisplayFrequency > 1 && static_cast<int32_t>(mode.dmDisplayFrequency) != cached)
            {
                cached = mode.dmDisplayFrequency;
                spdlog::info("FPSLimit: Monitor refresh rate is {} Hz, capping at {}", cached, cached - RefreshMargin);
            }
        }

        return cached;
    }

    int32_t Cap()
    {
        return nFPSLimit < 0 ? std::max(RefreshRate() - RefreshMargin, 0) : static_cast<int32_t>(nFPSLimit);
    }

    void __cdecl appSleep(float seconds)
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        auto deadline = now.QuadPart + static_cast<int64_t>(static_cast<double>(seconds) * qpcFrequency);

        for (;;)
        {
            QueryPerformanceCounter(&now);
            auto remaining = deadline - now.QuadPart;

            if (remaining <= 0)
                return;

            if (remaining <= spinTicks)
            {
                YieldProcessor();
                continue;
            }

            LARGE_INTEGER due;
            due.QuadPart = -((remaining - spinTicks) * 10000000 / qpcFrequency);
            if (SetWaitableTimer(hTimer, &due, 0, nullptr, nullptr, FALSE))
                WaitForSingleObject(hTimer, INFINITE);
        }
    }

    float __fastcall GetMaxTickRate(void* self, void*)
    {
        auto cap   = static_cast<float>(Cap());
        auto stock = shGetMaxTickRate.thiscall<float>(self);

        if (stock > 0.0f)
            return cap > 0.0f ? std::min(stock, cap) : stock;

        return cap;
    }
}

FEATURE(Engine, FPSLimit)
{
    if (!nFPSLimit)
        return;

    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    qpcFrequency = frequency.QuadPart;
    spinTicks    = qpcFrequency / 1000;

    // Before Windows 10, version 1803, fall back to a regular timer
    hTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

    if (!hTimer)
    {
        timeBeginPeriod(1);
        hTimer     = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        spinTicks *= 2;
        spdlog::warn("FPSLimit: High resolution timer unavailable, using a regular timer");
    }

    auto getMaxTickRate = GetProcAddress(GetModuleHandleW(L"Engine"), "?GetMaxTickRate@UGameEngine@@UAEMXZ");

    // Only the exe's main loop uses the frame limiter, DLL appSleep calls stay stock
    if (!getMaxTickRate || !hTimer ||
        !Memory::WriteIAT(GetModuleHandleW(nullptr), "Core.dll", "?appSleep@@YAXM@Z", reinterpret_cast<void*>(appSleep)))
    {
        spdlog::error("FPSLimit: GetMaxTickRate {}, timer {}", static_cast<void*>(getMaxTickRate), hTimer);
        return;
    }

    shGetMaxTickRate = safetyhook::create_inline(getMaxTickRate, GetMaxTickRate);

    if (nFPSLimit < 0)
        spdlog::info("FPSLimit: Frame rate capped to just under the monitor refresh rate");
    else
        spdlog::info("FPSLimit: Frame rate capped to {}", static_cast<int32_t>(nFPSLimit));
}
