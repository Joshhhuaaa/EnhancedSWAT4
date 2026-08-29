#include "stdafx.h"
#include "common.hpp"
#include "config.hpp"
#include "feature.hpp"

#include <d3d9.h>

static Config::Value nMsaa("Graphics", "MSAA", 0);

namespace
{
    // UD3DRenderDevice
    constexpr ptrdiff_t Direct3D       = 0x476c;
    constexpr ptrdiff_t Direct3DDevice = 0x4770;

    // FCanvasUtil::RI, and FD3DRenderInterface::RenDev behind it
    constexpr ptrdiff_t CanvasRI = 0x10;
    constexpr ptrdiff_t RenDev   = 0x04;

    SafetyHookInline shSetRes{};
    SafetyHookInline shReadPixels{};
    SafetyHookInline shCanvasFlush{};
    SafetyHookInline shCreateDevice{};
    SafetyHookInline shReset{};
    SafetyHookInline shPresent{};
    SafetyHookInline shStretchRect{};
    SafetyHookInline shSetDepthStencilSurface{};
    SafetyHookInline shGetBackBuffer{};

    D3DMULTISAMPLE_TYPE   forced = D3DMULTISAMPLE_NONE; // What the current device was asked for
    D3DPRESENT_PARAMETERS applied{};
    bool                  haveApplied = false;
    bool                  inReadPixels = false;

    // DEFAULT pool, released before Reset and CreateDevice
    IDirect3DSurface9*    resolved        = nullptr; // the back buffer resolved, lockable for ReadPixels
    IDirect3DSurface9*    substituteDepth = nullptr; // non-MS depth for a non-MS render target

    template <typename T>
    void Drop(T*& object)
    {
        if (object)
            object->Release();
        object = nullptr;
    }

    void ReleaseAll()
    {
        Drop(resolved);
        Drop(substituteDepth);
    }

    bool EnsureResolved(IDirect3DDevice9* device, const D3DSURFACE_DESC& desc)
    {
        D3DSURFACE_DESC have{};
        if (resolved)
            resolved->GetDesc(&have);
        if (resolved && (have.Width != desc.Width || have.Height != desc.Height || have.Format != desc.Format))
            ReleaseAll();

        return resolved ||
            SUCCEEDED(device->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, TRUE, &resolved, nullptr));
    }

    void Request(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE deviceType, D3DPRESENT_PARAMETERS& pp)
    {
        forced = D3DMULTISAMPLE_NONE;

        // The auto depth-stencil is created at the back buffer's sample count, so both formats
        // have to support the count
        for (auto level : { D3DMULTISAMPLE_8_SAMPLES, D3DMULTISAMPLE_4_SAMPLES, D3DMULTISAMPLE_2_SAMPLES })
        {
            if (level > nMsaa)
                continue;
            if (FAILED(d3d->CheckDeviceMultiSampleType(adapter, deviceType, pp.BackBufferFormat, pp.Windowed, level, nullptr)))
                continue;
            if (pp.EnableAutoDepthStencil &&
                FAILED(d3d->CheckDeviceMultiSampleType(adapter, deviceType, pp.AutoDepthStencilFormat, pp.Windowed, level, nullptr)))
                continue;

            forced = level;
            break;
        }

        if (forced == D3DMULTISAMPLE_NONE)
        {
            spdlog::warn("MSAA: {}x asked for, the device supports none of it", static_cast<int32_t>(nMsaa));
            return;
        }

        // The chain is already DISCARD and a lockable back buffer is the one thing D3D9 refuses to multisample
        pp.MultiSampleType    = forced;
        pp.MultiSampleQuality = 0;
        pp.Flags &= ~D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;

        if (forced < nMsaa)
            spdlog::info("MSAA: {}x asked for, {}x is the highest the device supports", static_cast<int32_t>(nMsaa), static_cast<int>(forced));
        else
            spdlog::info("MSAA: {}x", static_cast<int>(forced));
    }

    HRESULT __stdcall Reset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* pp)
    {
        IDirect3D9* d3d = nullptr;
        D3DDEVICE_CREATION_PARAMETERS creation{};
        if (SUCCEEDED(self->GetDirect3D(&d3d)) && SUCCEEDED(self->GetCreationParameters(&creation)))
            Request(d3d, creation.AdapterOrdinal, creation.DeviceType, *pp);
        if (d3d)
            d3d->Release();

        if (forced != D3DMULTISAMPLE_NONE && haveApplied && memcmp(&applied, pp, sizeof(applied)) == 0 &&
            self->TestCooperativeLevel() == D3D_OK)
        {
            spdlog::info("MSAA: Reset with unchanged parameters on a healthy device skipped");
            return D3D_OK;
        }

        applied     = *pp;
        haveApplied = true;

        ReleaseAll();
        return shReset.stdcall<HRESULT>(self, pp);
    }

    HRESULT __stdcall Present(IDirect3DDevice9* self, const RECT* source, const RECT* dest, HWND window, const RGNDATA* dirty)
    {
        if (forced != D3DMULTISAMPLE_NONE)
            source = dest = nullptr;
        return shPresent.stdcall<HRESULT>(self, source, dest, window, dirty);
    }

    // Glow downsizes with a filtered StretchRect, so multisampled sources must be resolved first.
    HRESULT __stdcall StretchRect(IDirect3DDevice9* self, IDirect3DSurface9* source, const RECT* sourceRect, IDirect3DSurface9* dest, const RECT* destRect, D3DTEXTUREFILTERTYPE filter)
    {
        D3DSURFACE_DESC sourceDesc{}, destDesc{};
        if (forced == D3DMULTISAMPLE_NONE ||
            FAILED(source->GetDesc(&sourceDesc)) || sourceDesc.MultiSampleType == D3DMULTISAMPLE_NONE ||
            FAILED(dest->GetDesc(&destDesc)) ||
            (!sourceRect && !destRect && filter == D3DTEXF_NONE && sourceDesc.Width == destDesc.Width && sourceDesc.Height == destDesc.Height))
            return shStretchRect.stdcall<HRESULT>(self, source, sourceRect, dest, destRect, filter);

        if (!EnsureResolved(self, sourceDesc))
            return D3DERR_INVALIDCALL;

        auto hr = shStretchRect.stdcall<HRESULT>(self, source, nullptr, resolved, nullptr, D3DTEXF_NONE);
        if (SUCCEEDED(hr))
            hr = shStretchRect.stdcall<HRESULT>(self, resolved, sourceRect, dest, destRect, filter);
        return hr;
    }

    // Glow uses a non-multisampled target with the multisampled depth buffer, so substitute the depth surface.
    HRESULT __stdcall SetDepthStencilSurface(IDirect3DDevice9* self, IDirect3DSurface9* depth)
    {
        IDirect3DSurface9* target = nullptr;
        D3DSURFACE_DESC targetDesc{}, depthDesc{}, haveDesc{};
        if (forced == D3DMULTISAMPLE_NONE || !depth || FAILED(self->GetRenderTarget(0, &target)))
            return shSetDepthStencilSurface.stdcall<HRESULT>(self, depth);

        target->GetDesc(&targetDesc);
        target->Release();
        depth->GetDesc(&depthDesc);
        if (targetDesc.MultiSampleType != D3DMULTISAMPLE_NONE || depthDesc.MultiSampleType == D3DMULTISAMPLE_NONE)
            return shSetDepthStencilSurface.stdcall<HRESULT>(self, depth);

        // A larger depth-stencil than the render target is legal
        if (substituteDepth)
            substituteDepth->GetDesc(&haveDesc);
        if (substituteDepth && (haveDesc.Width < targetDesc.Width || haveDesc.Height < targetDesc.Height || haveDesc.Format != depthDesc.Format))
            Drop(substituteDepth);

        if (!substituteDepth)
        {
            if (FAILED(self->CreateDepthStencilSurface(targetDesc.Width, targetDesc.Height, depthDesc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &substituteDepth, nullptr)))
                return shSetDepthStencilSurface.stdcall<HRESULT>(self, depth);

            spdlog::info("MSAA: depth substitute {}x{} for a non-multisampled render target", targetDesc.Width, targetDesc.Height);
        }

        return shSetDepthStencilSurface.stdcall<HRESULT>(self, substituteDepth);
    }

    // UE2's SHOT command read-locks the back buffer and crashes if it fails. It keeps its own
    // format conversion, so only the surface it is handed changes
    HRESULT __stdcall GetBackBuffer(IDirect3DDevice9* self, UINT swapChain, UINT index, D3DBACKBUFFER_TYPE type, IDirect3DSurface9** surface)
    {
        auto hr = shGetBackBuffer.stdcall<HRESULT>(self, swapChain, index, type, surface);

        D3DSURFACE_DESC desc{};
        if (!inReadPixels || FAILED(hr) || FAILED((*surface)->GetDesc(&desc)) || desc.MultiSampleType == D3DMULTISAMPLE_NONE ||
            !EnsureResolved(self, desc) || FAILED(self->StretchRect(*surface, nullptr, resolved, nullptr, D3DTEXF_NONE)))
            return hr;

        (*surface)->Release();
        resolved->AddRef();
        *surface = resolved;
        return hr;
    }

    void HookDevice(IDirect3DDevice9* device)
    {
        if (shReset)
            return;

        auto vtable = *reinterpret_cast<void***>(device);
        shReset                  = safetyhook::create_inline(vtable[16], Reset);
        shPresent                = safetyhook::create_inline(vtable[17], Present);
        shStretchRect            = safetyhook::create_inline(vtable[34], StretchRect);
        shSetDepthStencilSurface = safetyhook::create_inline(vtable[39], SetDepthStencilSurface);
        shGetBackBuffer          = safetyhook::create_inline(vtable[18], GetBackBuffer);
    }

    HRESULT __stdcall CreateDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE deviceType, HWND window, DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** device)
    {
        Request(self, adapter, deviceType, *pp);
        applied     = *pp;
        haveApplied = true;

        ReleaseAll();

        auto hr = shCreateDevice.stdcall<HRESULT>(self, adapter, deviceType, window, flags, pp, device);
        if (SUCCEEDED(hr))
            HookDevice(*device);
        return hr;
    }

    // Disable MSAA while drawing the canvas to avoid texture bleeding between glyphs
    void __fastcall CanvasFlush(uint8_t* self, void*)
    {
        auto ri     = *reinterpret_cast<uint8_t**>(self + CanvasRI);
        auto device = forced != D3DMULTISAMPLE_NONE && ri
                    ? *reinterpret_cast<IDirect3DDevice9**>(*reinterpret_cast<uint8_t**>(ri + RenDev) + Direct3DDevice)
                    : nullptr;

        if (device)
            device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE);
        shCanvasFlush.thiscall<void>(self);
        if (device)
            device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
    }

    void __fastcall ReadPixels(uint8_t* self, void*, void* viewport, void* pixels)
    {
        inReadPixels = forced != D3DMULTISAMPLE_NONE;
        shReadPixels.thiscall<void>(self, viewport, pixels);
        inReadPixels = false;
    }

    int __fastcall SetRes(uint8_t* self, void*, void* viewport, int newX, int newY, int fullscreen, int colorBytes, int saveSize)
    {
        auto d3d    = *reinterpret_cast<IDirect3D9**>(self + Direct3D);
        auto device = *reinterpret_cast<IDirect3DDevice9**>(self + Direct3DDevice);

        if (d3d && !shCreateDevice)
            shCreateDevice = safetyhook::create_inline((*reinterpret_cast<void***>(d3d))[16], CreateDevice);
        if (device)
            HookDevice(device);

        return shSetRes.thiscall<int>(self, viewport, newX, newY, fullscreen, colorBytes, saveSize);
    }
}

FEATURE(D3DDrv, Msaa)
{
    if (nMsaa < 2)
        return;

    auto d3dDrv      = GetModuleHandleW(L"D3DDrv");
    auto setRes      = GetProcAddress(d3dDrv, "?SetRes@UD3DRenderDevice@@UAEHPAVUViewport@@HHHHH@Z");
    auto readPixels  = GetProcAddress(d3dDrv, "?ReadPixels@UD3DRenderDevice@@UAEXPAVUViewport@@PAVFColor@@@Z");
    auto canvasFlush = GetProcAddress(GetModuleHandleW(L"Engine"), "?Flush@FCanvasUtil@@QAEXXZ");

    if (!setRes || !readPixels || !canvasFlush)
    {
        spdlog::error("MSAA: SetRes {}, ReadPixels {}, FCanvasUtil::Flush {}", static_cast<void*>(setRes), static_cast<void*>(readPixels), static_cast<void*>(canvasFlush));
        return;
    }

    shSetRes      = safetyhook::create_inline(setRes, SetRes);
    shReadPixels  = safetyhook::create_inline(readPixels, ReadPixels);
    shCanvasFlush = safetyhook::create_inline(canvasFlush, CanvasFlush);
    spdlog::info("MSAA: {}x requested, capped to what the device reports", static_cast<int32_t>(nMsaa));
}
