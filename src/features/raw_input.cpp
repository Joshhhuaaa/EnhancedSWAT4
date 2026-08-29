#include "stdafx.h"
#include "common.hpp"
#include "config.hpp"
#include "feature.hpp"
#include "script.hpp"

#include <hidusage.h>

static constexpr bool bRawInput = true;

static Config::Float fLookSensitivity("Input", "LookSensitivity", 1.0f);

namespace
{
    constexpr ptrdiff_t Window          = 0x1e0;  // WWindow*, WWindow::hWnd at +4
    constexpr ptrdiff_t WindowExpansion = 0x1e4;

    // Engine.Player packs bWindowsMouseAvailable, bShowWindowsMouse and bSuspendPrecaching into one
    // dword. Same offset in both builds - the UViewport growth is past it.
    constexpr ptrdiff_t PlayerFlags       = 0x38;
    constexpr uint8_t   bShowWindowsMouse = 0x2;

    // DIDEVICEOBJECTDATA.dwOfs, as HandleDIMouseInputEvent switches on it
    constexpr uint32_t DIMOFS_X       = 0x0;
    constexpr uint32_t DIMOFS_Y       = 0x4;
    constexpr uint32_t DIMOFS_Z       = 0x8;
    constexpr uint32_t DIMOFS_Button0 = 0xc;

    constexpr struct { USHORT Down, Up; uint32_t Button; } MouseButtons[] =
    {
        { RI_MOUSE_LEFT_BUTTON_DOWN,   RI_MOUSE_LEFT_BUTTON_UP,   0 },
        { RI_MOUSE_RIGHT_BUTTON_DOWN,  RI_MOUSE_RIGHT_BUTTON_UP,  1 },
        { RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, 2 },
        { RI_MOUSE_BUTTON_4_DOWN,      RI_MOUSE_BUTTON_4_UP,      3 },
        { RI_MOUSE_BUTTON_5_DOWN,      RI_MOUSE_BUTTON_5_UP,      4 },
    };

    SafetyHookInline shUpdateInput{};

    void (__fastcall* HandleDIMouseInputEvent)(void*, void*, uint32_t, uint32_t) = nullptr;
    void** ppMouse = nullptr;

    WNDPROC gameWndProc = nullptr;
    HWND    hGameWindow = nullptr;
    void*   gSelf       = nullptr;

    bool  bForeground = false;
    bool  bLegacyOff  = false;
    float deltaX      = 0.0f;
    float deltaY      = 0.0f;

    void RegisterRawInput(HWND hWnd, bool bNoLegacy)
    {
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
        rid.usUsage     = HID_USAGE_GENERIC_MOUSE;
        rid.dwFlags     = RIDEV_INPUTSINK | (bNoLegacy ? RIDEV_NOLEGACY : 0);
        rid.hwndTarget  = hWnd;

        if (!RegisterRawInputDevices(&rid, 1, sizeof(rid)))
            spdlog::error("RawInput: RegisterRawInputDevices failed, GetLastError {}", GetLastError());
    }

    LRESULT CALLBACK RawInputWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_INPUT && bForeground && gSelf)
        {
            RAWINPUT raw{};
            UINT size = sizeof(raw);

            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != UINT(-1) &&
                raw.header.dwType == RIM_TYPEMOUSE)
            {
                if (!(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
                {
                    deltaX += static_cast<float>(raw.data.mouse.lLastX) * fLookSensitivity;
                    deltaY += static_cast<float>(raw.data.mouse.lLastY) * fLookSensitivity;
                }

                auto flags = raw.data.mouse.usButtonFlags;

                for (auto& button : MouseButtons)
                {
                    if (flags & (button.Down | button.Up))
                        HandleDIMouseInputEvent(gSelf, nullptr, DIMOFS_Button0 + button.Button, (flags & button.Down) ? 0x80 : 0);
                }

                if (flags & RI_MOUSE_WHEEL)
                    HandleDIMouseInputEvent(gSelf, nullptr, DIMOFS_Z, static_cast<SHORT>(raw.data.mouse.usButtonData));
            }
        }

        return CallWindowProcW(gameWndProc, hWnd, msg, wParam, lParam);
    }

    void Attach(HWND hWnd)
    {
        hGameWindow = hWnd;
        gameWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(RawInputWndProc)));

        // Exclusive DirectInput and raw input compete for the same device, and every reader of the
        // static null-checks it, so retiring it outright is what keeps WM_INPUT arriving.
        if (ppMouse && *ppMouse)
        {
            auto mouse = *ppMouse;
            auto vtable = *reinterpret_cast<void***>(mouse);

            reinterpret_cast<HRESULT(__stdcall*)(void*)>(vtable[8])(mouse);
            *ppMouse = nullptr;

            spdlog::info("RawInput: DirectInput mouse unacquired and detached");
        }

        bForeground = GetForegroundWindow() == hWnd;
        bLegacyOff  = false;
        RegisterRawInput(hWnd, false);
        spdlog::info("RawInput: attached to hwnd {:#x}", reinterpret_cast<uintptr_t>(hWnd));
    }

    // MouseSmoothingMode and MouseAccelThreshold are UnrealScript, in a package with no native
    // override, so Core's reflection is the only way in. Written on the live PlayerInput every
    // frame rather than on the class defaults, because SEF's mouse settings call StaticSaveConfig
    // and a default we had written would be persisted into the user's own User.ini.
    void SuppressSmoothingAndAccel(void* self)
    {
        static void*    controller = nullptr;
        static void**   ppInput    = nullptr;
        static void*    input      = nullptr;
        static uint8_t* smoothing  = nullptr;
        static float*   accel      = nullptr;

        auto ppActor = static_cast<void**>(Script::Field(self, L"Actor"));
        auto owner   = ppActor ? *ppActor : nullptr;

        // PlayerInput is spawned with its PlayerController, so both are re-resolved on a level
        // change rather than every frame - the walk up PlayerController's chain is not cheap.
        if (owner != controller)
        {
            controller = owner;
            ppInput    = static_cast<void**>(Script::Field(owner, L"PlayerInput"));
        }

        auto current = ppInput ? *ppInput : nullptr;

        if (current != input)
        {
            input     = current;
            smoothing = static_cast<uint8_t*>(Script::Field(input, L"MouseSmoothingMode"));
            accel     = static_cast<float*>(Script::Field(input, L"MouseAccelThreshold"));

            if (input)
                spdlog::info("RawInput: PlayerInput {:#x}, MouseSmoothingMode {}, MouseAccelThreshold {}",
                    reinterpret_cast<uintptr_t>(input),
                    smoothing ? "cleared" : "NOT found", accel ? "cleared" : "NOT found");
        }

        if (smoothing)
            *smoothing = 0;

        if (accel)
            *accel = 0.0f;
    }

    void __fastcall UpdateInput(uint8_t* self, void*, int reset, float deltaSeconds)
    {
        gSelf = self;

        auto hWnd = *reinterpret_cast<HWND*>(*reinterpret_cast<uint8_t**>(self + (bExpansion ? WindowExpansion : Window)) + 4);

        if (hWnd && hWnd != hGameWindow)
            Attach(hWnd);

        SuppressSmoothingAndAccel(self);

        bForeground = GetForegroundWindow() == hGameWindow;

        shUpdateInput.thiscall<void>(self, reset, deltaSeconds);

        // Legacy mouse messages cost far more than the raw input they shadow at a high polling
        // rate, but ViewportWndProc's mouse-move case is what calls SetCursor and writes
        // WindowsMouseX/Y, both gated on bShowWindowsMouse - so they can only go while the game is
        // not showing the Windows cursor, which is exactly when nothing reads them.
        auto bNoLegacy = bForeground && !(*reinterpret_cast<uint8_t*>(self + PlayerFlags) & bShowWindowsMouse);

        if (hGameWindow && bNoLegacy != bLegacyOff)
        {
            bLegacyOff = bNoLegacy;
            RegisterRawInput(hGameWindow, bNoLegacy);
        }

        if (reset)
        {
            deltaX = deltaY = 0.0f;
            return;
        }

        // The fraction carries to the next frame so a multiplier never quantises motion away.
        auto x = static_cast<int32_t>(deltaX);
        auto y = static_cast<int32_t>(deltaY);
        deltaX -= static_cast<float>(x);
        deltaY -= static_cast<float>(y);

        if (x)
            HandleDIMouseInputEvent(self, nullptr, DIMOFS_X, x);

        if (y)
            HandleDIMouseInputEvent(self, nullptr, DIMOFS_Y, y);
    }
}

FEATURE(WinDrv, RawInput)
{
    if (!bRawInput)
        return;

    auto windrv = GetModuleHandleW(L"WinDrv");

    auto updateInput = GetProcAddress(windrv, "?UpdateInput@UWindowsViewport@@UAEXHM@Z");
    HandleDIMouseInputEvent = reinterpret_cast<decltype(HandleDIMouseInputEvent)>(
        GetProcAddress(windrv, "?HandleDIMouseInputEvent@UWindowsViewport@@QAEXKK@Z"));

    if (!updateInput || !HandleDIMouseInputEvent)
    {
        spdlog::error("RawInput: UpdateInput {}, HandleDIMouseInputEvent {}", (void*)updateInput, (void*)HandleDIMouseInputEvent);
        return;
    }

    ppMouse = reinterpret_cast<void**>(GetProcAddress(windrv, "?Mouse@UWindowsViewport@@2PAUIDirectInputDevice8W@@A"));
    if (!ppMouse)
        spdlog::error("RawInput: Mouse export not found, DirectInput mouse left running");

    shUpdateInput = safetyhook::create_inline(updateInput, UpdateInput);
    spdlog::info("RawInput: mouse taken from raw input, one delta per axis per frame, look x{}",
        static_cast<float>(fLookSensitivity));
}
