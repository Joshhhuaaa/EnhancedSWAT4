#include "stdafx.h"
#include "common.hpp"
#include "feature.hpp"
#include "script.hpp"

namespace
{
    // Smallest supported menu resolution
    constexpr DWORD MinWidth  = 800;
    constexpr DWORD MinHeight = 600;

    constexpr size_t ObjectClass = 0x24;

    // UE2's untyped TArray. FString includes a null terminator, so N characters use N + 1 elements
    struct FArray { void* Data; int Num; int Max; };

    using AddFn       = int (__thiscall*)(FArray*, int count, int elementSize);
    using AddZeroedFn = int (__thiscall*)(FArray*, int elementSize, int count);
    using EmptyFn     = void(__thiscall*)(FArray*, int elementSize, int slack);

    AddFn       arrayAdd{};
    AddZeroedFn arrayAddZeroed{};
    EmptyFn     arrayEmpty{};
    void*       guiConfigClass{};

    SafetyHookInline shConstructGuiConfig{};
    SafetyHookInline shLoadConfig{};

    const std::vector<std::wstring>& Modes()
    {
        static auto modes = []
        {
            std::vector<std::pair<DWORD, DWORD>> found;

            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);

            for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &mode); ++i)
                if (mode.dmBitsPerPel == 32 && mode.dmPelsWidth >= MinWidth && mode.dmPelsHeight >= MinHeight)
                    found.emplace_back(mode.dmPelsWidth, mode.dmPelsHeight);

            std::sort(found.begin(), found.end());
            found.erase(std::unique(found.begin(), found.end()), found.end());

            std::vector<std::wstring> choices;
            for (auto& [width, height] : found)
                choices.push_back(std::format(L"{}x{}", width, height));

            return choices;
        }();

        return modes;
    }

    void Apply(void* object, const char* how)
    {
        auto choices = static_cast<FArray*>(Script::Field(object, L"ScreenResolutionChoices"));
        if (!choices)
        {
            spdlog::error("ResolutionList: SwatGUIConfig has no ScreenResolutionChoices");
            return;
        }

        auto strings = static_cast<FArray*>(choices->Data);
        for (int i = 0; i < choices->Num; ++i)
            arrayEmpty(&strings[i], sizeof(wchar_t), 0);

        arrayEmpty(choices, sizeof(FArray), 0);
        arrayAddZeroed(choices, sizeof(FArray), static_cast<int>(Modes().size()));

        strings = static_cast<FArray*>(choices->Data);
        for (size_t i = 0; i < Modes().size(); ++i)
        {
            auto count = static_cast<int>(Modes()[i].size()) + 1;
            arrayAdd(&strings[i], count, sizeof(wchar_t));
            memcpy(strings[i].Data, Modes()[i].c_str(), count * sizeof(wchar_t));
        }

        spdlog::info("ResolutionList: SwatGUIConfig {} {} with {} modes", object, how, choices->Num);
    }

    void __cdecl ConstructGuiConfig(void* object)
    {
        shConstructGuiConfig.ccall<void>(object);

        if (object)
            Apply(object, "constructed");
    }

    void __fastcall LoadConfig(void* self, void*, int propagate, void* cls, const wchar_t* filename,
                               const wchar_t* section, int a, int b)
    {
        shLoadConfig.thiscall<void>(self, propagate, cls, filename, section, a, b);

        if (*reinterpret_cast<void**>(static_cast<uint8_t*>(self) + ObjectClass) == guiConfigClass)
            Apply(self, "reloaded");
    }
}

FEATURE(SwatGame, ResolutionList)
{
    if (Modes().empty())
    {
        spdlog::error("ResolutionList: the display reported no usable modes");
        return;
    }

    auto core     = GetModuleHandleW(L"Core");
    auto swatGame = GetModuleHandleW(L"SwatGame");

    guiConfigClass = GetProcAddress(swatGame, "?PrivateStaticClass@USwatGUIConfig@@0VUClass@@A");
    arrayAdd       = reinterpret_cast<AddFn>(GetProcAddress(core, "?Add@FArray@@QAEHHH@Z"));
    arrayAddZeroed = reinterpret_cast<AddZeroedFn>(GetProcAddress(core, "?AddZeroed@FArray@@QAEHHH@Z"));
    arrayEmpty     = reinterpret_cast<EmptyFn>(GetProcAddress(core, "?Empty@FArray@@QAEXHH@Z"));

    auto constructGuiConfig = GetProcAddress(swatGame, "?InternalConstructor@USwatGUIConfig@@SAXPAX@Z");
    auto loadConfig         = GetProcAddress(core, "?LoadConfig@UObject@@QAEXHPAVUClass@@PBG1HH@Z");

    if (!guiConfigClass || !arrayAdd || !arrayAddZeroed || !arrayEmpty || !constructGuiConfig || !loadConfig)
    {
        spdlog::error("ResolutionList: Core reflection exports not found");
        return;
    }

    shConstructGuiConfig = safetyhook::create_inline(constructGuiConfig, ConstructGuiConfig);
    shLoadConfig         = safetyhook::create_inline(loadConfig, LoadConfig);
}
