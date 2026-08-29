#include "stdafx.h"
#include "common.hpp"
#include "config.hpp"
#include "feature.hpp"
#include "script.hpp"

static Config::Value nOptiwandResolution("Graphics", "OptiwandResolution", 256);

namespace
{
    SafetyHookInline shSetSize{};
    int size = 256;

    void __fastcall SetSize(void* self, void*, int width, int height)
    {
        auto ppClient = static_cast<void**>(Script::Field(self, L"Client"));
        auto client   = ppClient ? *ppClient : nullptr;
        auto sizeX    = static_cast<int*>(Script::Field(client, L"SizeX"));
        auto sizeY    = static_cast<int*>(Script::Field(client, L"SizeY"));

        if (sizeX && sizeY && *sizeX == width && *sizeY == height)
        {
            *sizeX = *sizeY = width = height = size;
            spdlog::info("OptiwandResolution: {}x{}", width, height);
        }

        shSetSize.thiscall<void>(self, width, height);
    }
}

FEATURE(Engine, OptiwandResolution)
{
    if (nOptiwandResolution <= 256)
        return;

    for (size = 256; size < std::min<int>(nOptiwandResolution, 4096); size *= 2);

    auto setSize = GetProcAddress(GetModuleHandleW(L"Engine"), "?SetSize@UScriptedTexture@@QAEXHH@Z");

    if (!setSize)
    {
        spdlog::error("OptiwandResolution: UScriptedTexture::SetSize not found");
        return;
    }

    shSetSize = safetyhook::create_inline(setSize, SetSize);
    spdlog::info("OptiwandResolution: {} requested, {} applied", static_cast<int32_t>(nOptiwandResolution), size);
}
