#include "stdafx.h"
#include "common.hpp"
#include "feature.hpp"

#include <d3d9.h>

namespace
{
    constexpr ptrdiff_t Direct3DDevice = 0x4770;
    constexpr ptrdiff_t Actor     = 0x30;
    constexpr ptrdiff_t MyHud     = 0x698;
    constexpr ptrdiff_t Movie     = 0x41c;
    constexpr ptrdiff_t MoviePosX = 0x420;
    constexpr ptrdiff_t MoviePosY = 0x424;
    constexpr ptrdiff_t FMovie    = 0x28;
    constexpr ptrdiff_t Bink      = 0xc;    // FBinkMovie::Bink, an HBINK: Width, Height first
    constexpr ptrdiff_t IsPlaying = 0x18;   // FMovie vtable

    SafetyHookInline shRenderMovie{};
    SafetyHookInline shSetRes{};

    IDirect3DSurface9*    offscreen  = nullptr; // DEFAULT, lockable, the back buffer's size and format
    IDirect3DTexture9*    movieTex   = nullptr;
    IDirect3DStateBlock9* movieState = nullptr;

    template <typename T>
    void Drop(T*& object)
    {
        if (object)
            object->Release();
        object = nullptr;
    }

    void ReleaseAll()
    {
        Drop(offscreen);
        Drop(movieTex);
        Drop(movieState);
    }

    template <typename T>
    T At(void* base, ptrdiff_t offset)
    {
        return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
    }

    void __fastcall RenderMovie(uint8_t* self, void*, uint8_t* viewport)
    {
        auto device = At<IDirect3DDevice9*>(self, Direct3DDevice);
        auto actor  = viewport ? At<uint8_t*>(viewport, Actor) : nullptr;
        auto hud    = actor ? At<uint8_t*>(actor, MyHud) : nullptr;
        auto movie  = hud ? At<uint8_t*>(hud, Movie) : nullptr;
        auto fmovie = movie ? At<uint8_t*>(movie, FMovie) : nullptr;
        auto bink   = fmovie ? At<uint8_t*>(fmovie, Bink) : nullptr;
        auto isPlaying = fmovie ? reinterpret_cast<int(__fastcall*)(void*, void*)>(At<void**>(fmovie, 0)[IsPlaying / sizeof(void*)]) : nullptr;

        IDirect3DSurface9* target     = nullptr;
        IDirect3DSurface9* texSurface = nullptr;
        D3DSURFACE_DESC    desc{};

        if (!device || !bink || !isPlaying(fmovie, nullptr) || FAILED(device->GetRenderTarget(0, &target)))
        {
            shRenderMovie.thiscall<void>(self, viewport);
            return;
        }

        target->GetDesc(&desc);

        // The movie's rect in the back buffer, and where it should be drawn
        const int x = At<int>(hud, MoviePosX), y = At<int>(hud, MoviePosY);
        const int w = At<int>(bink, 0),        h = At<int>(bink, 4);
        const float scale = std::min(static_cast<float>(desc.Width) / w, static_cast<float>(desc.Height) / h);
        const int dw = static_cast<int>(w * scale), dh = static_cast<int>(h * scale);
        const int dx = (static_cast<int>(desc.Width) - dw) / 2, dy = (static_cast<int>(desc.Height) - dh) / 2;

        const bool stock = desc.MultiSampleType == D3DMULTISAMPLE_NONE && dx == x && dy == y && dw == w && dh == h;

        if (stock || x < 0 || y < 0 || x + w > static_cast<int>(desc.Width) || y + h > static_cast<int>(desc.Height) ||
            (!offscreen && FAILED(device->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, TRUE, &offscreen, nullptr))) ||
            (!movieTex && FAILED(device->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &movieTex, nullptr))) ||
            (!movieState && FAILED(device->CreateStateBlock(D3DSBT_ALL, &movieState))) ||
            FAILED(movieTex->GetSurfaceLevel(0, &texSurface)))
        {
            target->Release();
            shRenderMovie.thiscall<void>(self, viewport);
            return;
        }

        // Captured before the target switch, so Apply puts back the game's viewport as well
        movieState->Capture();

        device->SetRenderTarget(0, offscreen);
        shRenderMovie.thiscall<void>(self, viewport);
        device->SetRenderTarget(0, target);

        if (SUCCEEDED(device->StretchRect(offscreen, nullptr, texSurface, nullptr, D3DTEXF_NONE)))
        {
            bool ownScene = SUCCEEDED(device->BeginScene());

            device->SetVertexShader(nullptr);
            device->SetPixelShader(nullptr);
            device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
            device->SetTexture(0, movieTex);
            device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
            device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
            device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);
            device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
            device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
            device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
            device->SetRenderState(D3DRS_FOGENABLE, FALSE);
            device->SetRenderState(D3DRS_LIGHTING, FALSE);
            device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
            device->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0000000F);
            device->SetRenderState(D3DRS_SRGBWRITEENABLE, 0);

            struct Vertex { float x, y, z, w, u, v; };
            const float left = dx - 0.5f, top = dy - 0.5f, right = dx + dw - 0.5f, bottom = dy + dh - 0.5f;
            const float u0 = static_cast<float>(x) / desc.Width, v0 = static_cast<float>(y) / desc.Height;
            const float u1 = static_cast<float>(x + w) / desc.Width, v1 = static_cast<float>(y + h) / desc.Height;
            const Vertex quad[4] =
            {
                { left,  top,    0.0f, 1.0f, u0, v0 },
                { right, top,    0.0f, 1.0f, u1, v0 },
                { left,  bottom, 0.0f, 1.0f, u0, v1 },
                { right, bottom, 0.0f, 1.0f, u1, v1 },
            };
            device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));

            if (ownScene)
                device->EndScene();
        }

        movieState->Apply();
        texSurface->Release();
        target->Release();
    }

    // DEFAULT-pool objects alive across Reset make it fail, and a new device cannot use the old ones
    int __fastcall SetRes(void* self, void*, void* viewport, int newX, int newY, int fullscreen, int colorBytes, int saveSize)
    {
        ReleaseAll();
        return shSetRes.thiscall<int>(self, viewport, newX, newY, fullscreen, colorBytes, saveSize);
    }
}

FEATURE(D3DDrv, DirectMovie)
{
    auto d3dDrv      = GetModuleHandleW(L"D3DDrv");
    auto renderMovie = GetProcAddress(d3dDrv, "?RenderMovie@UD3DRenderDevice@@UAEXPAVUViewport@@@Z");
    auto setRes      = GetProcAddress(d3dDrv, "?SetRes@UD3DRenderDevice@@UAEHPAVUViewport@@HHHHH@Z");

    if (!renderMovie || !setRes)
    {
        spdlog::error("DirectMovie: RenderMovie {}, SetRes {}", static_cast<void*>(renderMovie), static_cast<void*>(setRes));
        return;
    }

    shRenderMovie = safetyhook::create_inline(renderMovie, RenderMovie);
    shSetRes      = safetyhook::create_inline(setRes, SetRes);
    spdlog::info("DirectMovie: Scaled to the screen and drawn from an offscreen copy");
}
