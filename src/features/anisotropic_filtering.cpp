#include "stdafx.h"
#include "common.hpp"
#include "config.hpp"
#include "feature.hpp"

static Config::Value nAnisotropy("Graphics", "AnisotropicFiltering", 16);

namespace
{
    // UD3DRenderDevice::LevelOfAnisotropy, read off the CPP_PROPERTY offset its UIntProperty is
    // registered with in the class constructor.
    constexpr ptrdiff_t LevelOfAnisotropy = 0x4168;

    SafetyHookInline shSetRes{};

    int __fastcall SetRes(uint8_t* self, void* edx, void* viewport, int newX, int newY, int fullscreen, int a5, int a6)
    {
        *reinterpret_cast<int32_t*>(self + LevelOfAnisotropy) = nAnisotropy;
        return shSetRes.thiscall<int>(self, viewport, newX, newY, fullscreen, a5, a6);
    }
}

FEATURE(D3DDrv, AnisotropicFiltering)
{
    if (nAnisotropy <= 1)
        return;

    auto setRes = GetProcAddress(GetModuleHandleW(L"D3DDrv"),
        "?SetRes@UD3DRenderDevice@@UAEHPAVUViewport@@HHHHH@Z");

    if (!setRes)
    {
        spdlog::error("AnisotropicFiltering: UD3DRenderDevice::SetRes not found");
        return;
    }

    shSetRes = safetyhook::create_inline(setRes, SetRes);
    spdlog::info("AnisotropicFiltering: {}x, capped to what the device reports",
                 static_cast<int32_t>(nAnisotropy));
}
