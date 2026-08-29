#include "stdafx.h"
#include "common.hpp"
#include "feature.hpp"

namespace
{
    BOOL WINAPI NoGammaRamp(HDC, LPVOID)
    {
        return TRUE;
    }
}

EDITOR_FEATURE(D3DDrv, EditorGamma)
{
    if (!Memory::WriteIAT(GetModuleHandleW(L"D3DDrv"), "GDI32.dll", "SetDeviceGammaRamp", NoGammaRamp))
    {
        spdlog::error("EditorGamma: SetDeviceGammaRamp import not found");
        return;
    }

    spdlog::info("EditorGamma: Desktop gamma left alone");
}
