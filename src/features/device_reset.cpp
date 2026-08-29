#include "stdafx.h"
#include "common.hpp"
#include "feature.hpp"

namespace
{
    SafetyHookInline shViewportWndProc{};
    SafetyHookInline shSetRes{};

    UINT wndProcMsg   = 0;
    int  wndProcDepth = 0;
    bool bInSetRes    = false;

    LONG __fastcall ViewportWndProc(void* self, void*, UINT msg, UINT wParam, LONG lParam)
    {
        auto previous = wndProcMsg;
        wndProcMsg = msg;
        wndProcDepth++;

        auto result = shViewportWndProc.thiscall<LONG>(self, msg, wParam, lParam);

        wndProcDepth--;
        wndProcMsg = previous;

        return result;
    }

    // D3D can call the window procedure during Reset/CreateDevice, and WM_SIZE can re-enter SetRes.
    // Defer nested SetRes calls to Lock on the next frame.
    int __fastcall SetRes(void* self, void*, void* viewport, int newX, int newY, int fullscreen, int colorBytes, int saveSize)
    {
        if (bInSetRes || wndProcDepth > 1)
        {
            spdlog::warn("DeviceReset: SetRes re-entered from message {:#x} (in SetRes {}, depth {}), left to Lock", wndProcMsg, bInSetRes, wndProcDepth);
            return 1;
        }

        bInSetRes = true;
        auto result = shSetRes.thiscall<int>(self, viewport, newX, newY, fullscreen, colorBytes, saveSize);
        bInSetRes = false;

        return result;
    }
}

FEATURE(D3DDrv, DeviceReset)
{
    auto d3dDrv  = GetModuleHandleW(L"D3DDrv");
    auto setRes  = GetProcAddress(d3dDrv, "?SetRes@UD3DRenderDevice@@UAEHPAVUViewport@@HHHHH@Z");
    auto wndProc = GetProcAddress(GetModuleHandleW(L"WinDrv"), "?ViewportWndProc@UWindowsViewport@@QAEJIIJ@Z");

    // CMP [EDX],EBX / JZ past Reset: GIsEditor == 0 skips straight to Release.
    auto resetGate = hook::module_pattern(d3dDrv, "39 1A 74 13 8B 07 8B 08 8D 96 74 47 00 00");

    if (!setRes || !wndProc || resetGate.empty())
    {
        spdlog::error("DeviceReset: SetRes {}, ViewportWndProc {}, Reset gate {}", static_cast<void*>(setRes), static_cast<void*>(wndProc), resetGate.size());
        return;
    }

    injector::MakeNOP(resetGate.get_first(2), 2, true);

    shViewportWndProc = safetyhook::create_inline(wndProc, ViewportWndProc);
    shSetRes          = safetyhook::create_inline(setRes, SetRes);
    spdlog::info("DeviceReset: Device reset in place instead of recreated, nested SetRes refused");
}
