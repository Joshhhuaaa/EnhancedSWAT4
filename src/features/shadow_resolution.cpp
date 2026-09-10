#include "stdafx.h"
#include "common.hpp"
#include "config.hpp"
#include "feature.hpp"
#include "script.hpp"

static Config::Value nShadowResolution("Graphics", "ShadowResolution", 256);

namespace
{
    SafetyHookInline shUpdateDetailSetting{};
    int size = 256;

    void __fastcall UpdateDetailSetting(void* self, void*, void* stack, void* result)
    {
        if (auto resolution = static_cast<int*>(Script::Field(self, L"Resolution")))
            *resolution = size;

        shUpdateDetailSetting.thiscall<void>(self, stack, result);
    }
}

FEATURE(Engine, ShadowResolution)
{
    if (nShadowResolution <= 256)
        return;

    for (size = 256; size < std::min<int>(nShadowResolution, 2048); size *= 2);

    auto updateDetailSetting = GetProcAddress(GetModuleHandleW(L"Engine"), "?execUpdateDetailSetting@AShadowProjector@@QAEXAAUFFrame@@QAX@Z");

    if (!updateDetailSetting)
    {
        spdlog::error("ShadowResolution: AShadowProjector::UpdateDetailSetting not found");
        return;
    }

    shUpdateDetailSetting = safetyhook::create_inline(updateDetailSetting, UpdateDetailSetting);
    spdlog::info("ShadowResolution: {} requested, {} applied", static_cast<int32_t>(nShadowResolution), size);
}
