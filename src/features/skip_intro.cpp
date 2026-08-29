#include "stdafx.h"
#include "common.hpp"
#include "config.hpp"
#include "feature.hpp"

static Config::Value bSkipIntro("General", "SkipIntro", false);

namespace
{
    // Engine.Repo's three bools are packed into one DWORD at 0x30 in declaration order:
    // InitAsListenServer, InitAsDedicatedServer, InitWithoutIntroMenu.
    constexpr uintptr_t RepoBools = 0x30;
    constexpr uint32_t  InitWithoutIntroMenu = 0x4;

    SafetyHookInline shConstructRepo{};

    void __cdecl ConstructRepo(void* repo)
    {
        shConstructRepo.ccall<void>(repo);

        if (repo)
            *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(repo) + RepoBools) |= InitWithoutIntroMenu;
    }
}

FEATURE(SwatGame, SkipIntro)
{
    if (!bSkipIntro)
        return;

    auto swatGame = GetModuleHandleW(L"SwatGame");
    auto constructRepo = GetProcAddress(swatGame, "?InternalConstructor@USwatRepo@@SAXPAX@Z");

    if (!constructRepo)
    {
        spdlog::error("SkipIntro: USwatRepo::InternalConstructor not found");
        return;
    }

    shConstructRepo = safetyhook::create_inline(constructRepo, ConstructRepo);
    spdlog::info("SkipIntro: Intro sequence skipped");
}
