#include "stdafx.h"
#include "common.hpp"
#include "config.hpp"
#include "feature.hpp"

static Config::Value nDisplayMode("Graphics", "DisplayMode", 0);

namespace
{
    enum { Fullscreen, Borderless, Windowed };

    // UWindowsViewport::Window, a WWindow* with hWnd at +4. The expansion's UViewport is 4 bytes longer
    constexpr ptrdiff_t Window    = 0x1e0;
    constexpr ptrdiff_t Expansion = 0x4;

    SafetyHookInline shSetRes{};
    SafetyHookInline shViewportWndProc{};
    void (__fastcall* SetMouseCapture)(void* self, void* edx, int capture, int clip, int onlyFocus) = nullptr;

    template <typename T>
    T& At(uint8_t* viewport, ptrdiff_t offset)
    {
        return *reinterpret_cast<T*>(viewport + offset + (bExpansion ? Expansion : 0));
    }

    HWND WindowOf(uint8_t* viewport)
    {
        auto window = At<uint8_t*>(viewport, Window);
        return window ? *reinterpret_cast<HWND*>(window + 4) : nullptr;
    }

    LONG __fastcall ViewportWndProc(uint8_t* self, void*, UINT msg, UINT wParam, LONG lParam)
    {
        if (msg == WM_MOUSEMOVE || msg == WM_INPUT)
        {
            auto hWnd        = WindowOf(self);
            auto bForeground = hWnd && GetForegroundWindow() == hWnd;

            // Ignore mouse movement from an unfocused viewport
            if (msg == WM_MOUSEMOVE && !bForeground)
                return 0;

            // Keep the game's cursor captured while the Windows cursor is visible
            CURSORINFO cursor{};
            cursor.cbSize = sizeof(cursor);
            if (bForeground && GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING))
                SetMouseCapture(self, nullptr, 1, 1, 0);
        }

        return shViewportWndProc.thiscall<LONG>(self, msg, wParam, lParam);
    }

    int __fastcall SetRes(void* self, void*, uint8_t* viewport, int newX, int newY, int, int colorBytes, int saveSize)
    {
        auto hWnd = WindowOf(viewport);

        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);

        auto bBorderless = nDisplayMode == Borderless && hWnd &&
                           GetMonitorInfoW(MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY), &monitor);

        if (bBorderless)
        {
            newX = monitor.rcMonitor.right - monitor.rcMonitor.left;
            newY = monitor.rcMonitor.bottom - monitor.rcMonitor.top;
        }

        auto result = shSetRes.thiscall<int>(self, viewport, newX, newY, 0, colorBytes, saveSize);

        // ResizeViewport restores the window style and position, so apply borderless style afterwards.
        // Capture last so SetMouseCapture sees the final client rect.
        if (result && bBorderless)
        {
            SetWindowLongW(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowPos(hWnd, nullptr, monitor.rcMonitor.left, monitor.rcMonitor.top, newX, newY, SWP_NOZORDER | SWP_FRAMECHANGED);
            SetMouseCapture(viewport, nullptr, 1, 1, 0);
        }

        return result;
    }
}

FEATURE(D3DDrv, DisplayMode)
{
    if (nDisplayMode == Fullscreen)
        return;

    auto winDrv          = GetModuleHandleW(L"WinDrv");
    auto setRes          = GetProcAddress(GetModuleHandleW(L"D3DDrv"), "?SetRes@UD3DRenderDevice@@UAEHPAVUViewport@@HHHHH@Z");
    auto wndProc         = GetProcAddress(winDrv, "?ViewportWndProc@UWindowsViewport@@QAEJIIJ@Z");
    SetMouseCapture = reinterpret_cast<decltype(SetMouseCapture)>(GetProcAddress(winDrv, "?SetMouseCapture@UWindowsViewport@@UAEXHHH@Z"));

    if (!setRes || !wndProc || !SetMouseCapture)
    {
        spdlog::error("DisplayMode: SetRes {}, ViewportWndProc {}, SetMouseCapture {}", static_cast<void*>(setRes), static_cast<void*>(wndProc), reinterpret_cast<void*>(SetMouseCapture));
        return;
    }

    shSetRes          = safetyhook::create_inline(setRes, SetRes);
    shViewportWndProc = safetyhook::create_inline(wndProc, ViewportWndProc);
    spdlog::info("DisplayMode: {}", nDisplayMode == Borderless ? "borderless at the monitor resolution" : "windowed at the game resolution");
}
