#include "stdafx.h"
#include "common.hpp"
#include "config.hpp"
#include "feature.hpp"
#include "script.hpp"

static Config::Float fFieldOfView("Graphics", "FieldOfView", 1.0f);

namespace
{
    constexpr double pi         = 3.14159265358979323846;
    constexpr double BaseAspect = 4.0 / 3.0;
    constexpr double BaseFOV    = 85.0;

    constexpr float MinFOV = 85.0f;
    constexpr float MaxFOV = 120.0f;

    // FSceneNode's viewport render target (Optiwand's 256x256 screen, flashbang, etc.)
    constexpr ptrdiff_t ViewportRenderTarget         = 0x108;
    constexpr ptrdiff_t ViewportRenderTargetExpanded = 0x10c;

    constexpr size_t GetWidth  = 7;
    constexpr size_t GetHeight = 8;

    SafetyHookInline shFCameraSceneNode{};

    void PinFOV(void* viewport)
    {
        static void*  controller = nullptr;
        static float* pBaseFOV   = nullptr;
        static float* pFPFOV     = nullptr;

        auto ppActor = static_cast<void**>(Script::Field(viewport, L"Actor"));
        auto owner   = ppActor ? *ppActor : nullptr;

        if (owner != controller)
        {
            controller = owner;
            pBaseFOV   = static_cast<float*>(Script::Field(owner, L"BaseFOV"));

            // Keep SEF's FOV settings at vanilla defaults, the display FOV hook owns the final FOV
            if (owner && !pFPFOV)
                pFPFOV = static_cast<float*>(Script::Default(L"FOVSettings", L"FPFOV"));

            if (owner)
                spdlog::info("FOV: PlayerController {:#x}, BaseFOV {}, FPFOV {}",
                    reinterpret_cast<uintptr_t>(owner),
                    pBaseFOV ? "pinned" : "NOT found",
                    pFPFOV ? "pinned" : "absent, vanilla hands");
        }

        if (pBaseFOV)
            *pBaseFOV = static_cast<float>(BaseFOV);

        if (pFPFOV)
            *pFPFOV = static_cast<float>(BaseFOV);
    }

    float AdjustFOV(float fov, float aspect)
    {
        double scale;

        if (fFieldOfView == 1.0f)
            scale = aspect / BaseAspect;
        else
            scale = std::tan(std::clamp(static_cast<float>(fFieldOfView), MinFOV, MaxFOV) / 2.0 * (pi / 180.0))
                  / std::tan(BaseFOV / 2.0 * (pi / 180.0));

        return static_cast<float>(std::round(2.0 * std::atan(scale * std::tan(fov / 2.0 * (pi / 180.0))) * (180.0 / pi) * 100.0) / 100.0);
    }

    void* __fastcall FCameraSceneNode(void* self, void*, uint8_t* viewport, void** renderTarget, void* actor,
                                      float locX, float locY, float locZ,
                                      int pitch, int yaw, int roll, float fov)
    {
        if (renderTarget == reinterpret_cast<void**>(viewport + (bExpansion ? ViewportRenderTargetExpanded : ViewportRenderTarget)))
        {
            PinFOV(viewport);

            auto vtable = static_cast<void**>(*renderTarget);
            auto width  = reinterpret_cast<int(__thiscall*)(void*)>(vtable[GetWidth])(renderTarget);
            auto height = reinterpret_cast<int(__thiscall*)(void*)>(vtable[GetHeight])(renderTarget);

            if (height > 0)
                fov = AdjustFOV(fov, static_cast<float>(width) / static_cast<float>(height));
        }

        return shFCameraSceneNode.thiscall<void*>(self, viewport, renderTarget, actor, locX, locY, locZ, pitch, yaw, roll, fov);
    }
}

FEATURE(Engine, FOV)
{
    if (fFieldOfView == 0.0f)
        return;

    auto cameraSceneNode = GetProcAddress(GetModuleHandleW(L"Engine"),
        "??0FCameraSceneNode@@QAE@PAVUViewport@@PAVFRenderTarget@@PAVAActor@@VFVector@@VFRotator@@M@Z");

    if (!cameraSceneNode)
    {
        spdlog::error("FOV: FCameraSceneNode constructor not found");
        return;
    }

    shFCameraSceneNode = safetyhook::create_inline(cameraSceneNode, FCameraSceneNode);

    if (fFieldOfView == 1.0f)
        spdlog::info("FOV: Horizontal FOV corrected to hor+ against a 4:3 reference");
    else
        spdlog::info("FOV: Horizontal FOV forced to {}", std::clamp(static_cast<float>(fFieldOfView), MinFOV, MaxFOV));
}
