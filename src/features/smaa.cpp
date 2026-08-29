#include "stdafx.h"
#include "common.hpp"
#include "config.hpp"
#include "feature.hpp"

#include <d3d9.h>

#include "smaa_shaders.h"
#include "smaa_textures.h"

static Config::Value bSmaa("Graphics", "SMAA", true);

// Applied during Present, so HUD and menus are filtered. The UE2 shot command captures before this.
namespace
{
    // UD3DRenderDevice
    constexpr ptrdiff_t Direct3DDevice = 0x4770;

    SafetyHookInline shPresent{};
    SafetyHookInline shSetRes{};

    IDirect3DTexture9* sceneCopy = nullptr; // backbuffer format
    IDirect3DTexture9* edgesTex  = nullptr; // A8R8G8B8
    IDirect3DTexture9* blendTex  = nullptr; // A8R8G8B8
    IDirect3DTexture9* areaTex   = nullptr;
    IDirect3DTexture9* searchTex = nullptr;
    IDirect3DPixelShader9*       ps[3]      = {};
    IDirect3DVertexShader9*      vs[3]      = {};
    IDirect3DVertexDeclaration9* vertexDecl = nullptr;
    IDirect3DStateBlock9*        stateBlock = nullptr;

    UINT postWidth = 0, postHeight = 0;
    bool unsupported = false;

    template <typename T>
    void Drop(T*& object)
    {
        if (object)
            object->Release();
        object = nullptr;
    }

    void ReleaseAll()
    {
        Drop(sceneCopy);
        Drop(edgesTex);
        Drop(blendTex);
        Drop(areaTex);
        Drop(searchTex);
        for (auto& shader : ps) Drop(shader);
        for (auto& shader : vs) Drop(shader);
        Drop(vertexDecl);
        Drop(stateBlock);
        postWidth = postHeight = 0;
    }

    bool Upload(IDirect3DDevice9* device, UINT width, UINT height, D3DFORMAT format, const uint8_t* pixels, UINT pitch, IDirect3DTexture9*& texture)
    {
        D3DLOCKED_RECT lr{};
        if (FAILED(device->CreateTexture(width, height, 1, 0, format, D3DPOOL_MANAGED, &texture, nullptr)) ||
            FAILED(texture->LockRect(0, &lr, nullptr, 0)))
            return false;

        for (UINT y = 0; y < height; y++)
            memcpy(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch, pixels + y * pitch, pitch);

        texture->UnlockRect(0);
        return true;
    }

    bool EnsureResources(IDirect3DDevice9* device, UINT width, UINT height, D3DFORMAT backBufferFormat)
    {
        // A windowed resize without a Reset
        if (postWidth != 0 && (postWidth != width || postHeight != height))
            ReleaseAll();

        if (vs[0] == nullptr)
        {
            D3DCAPS9 caps{};
            if (FAILED(device->GetDeviceCaps(&caps)) ||
                caps.PixelShaderVersion < D3DPS_VERSION(3, 0) || caps.VertexShaderVersion < D3DVS_VERSION(3, 0))
            {
                spdlog::warn("SMAA: needs shader model 3.0, device reports ps {:#x} vs {:#x}", caps.PixelShaderVersion, caps.VertexShaderVersion);
                unsupported = true;
                return false;
            }

            static const BYTE* const vsCode[3] = { SmaaVS0, SmaaVS1, SmaaVS2 };
            static const BYTE* const psCode[3] = { SmaaPS0, SmaaPS1, SmaaPS2 };

            for (int i = 0; i < 3; i++)
            {
                if (FAILED(device->CreateVertexShader(reinterpret_cast<const DWORD*>(vsCode[i]), &vs[i])) ||
                    FAILED(device->CreatePixelShader(reinterpret_cast<const DWORD*>(psCode[i]), &ps[i])))
                {
                    spdlog::error("SMAA: pass {} shaders failed to create", i);
                    unsupported = true;
                    ReleaseAll();
                    return false;
                }
            }

            // The DX9_*VS entries pass POSITION straight through, so the quad is already clip space
            static const D3DVERTEXELEMENT9 declElements[] =
            {
                { 0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
                { 0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
                D3DDECL_END()
            };

            if (FAILED(device->CreateVertexDeclaration(declElements, &vertexDecl)) ||
                !Upload(device, 160, 560, D3DFMT_A8L8, SmaaAreaTex, 320, areaTex) ||
                !Upload(device, 64, 16, D3DFMT_L8, SmaaSearchTex, 64, searchTex))
            {
                spdlog::error("SMAA: lookup textures failed to create");
                unsupported = true;
                ReleaseAll();
                return false;
            }
        }

        // Match the backbuffer format so StretchRect is a plain copy
        if (sceneCopy == nullptr &&
            FAILED(device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, backBufferFormat, D3DPOOL_DEFAULT, &sceneCopy, nullptr)))
            return false;
        // SMAA requires RGBA intermediates
        if (edgesTex == nullptr &&
            FAILED(device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &edgesTex, nullptr)))
            return false;
        if (blendTex == nullptr &&
            FAILED(device->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &blendTex, nullptr)))
            return false;
        // DEFAULT-pool resources may fail transiently and are retried on the next Present
        if (stateBlock == nullptr &&
            FAILED(device->CreateStateBlock(D3DSBT_ALL, &stateBlock)))
            return false;

        if (postWidth != width || postHeight != height)
            spdlog::info("SMAA: running at {}x{}", width, height);

        postWidth  = width;
        postHeight = height;
        return true;
    }

    void ApplyInvariantState(IDirect3DDevice9* device, UINT width, UINT height)
    {
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
        device->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0000000F);
        device->SetRenderState(D3DRS_FOGENABLE, FALSE);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
        device->SetRenderState(D3DRS_SRGBWRITEENABLE, 0);

        // A cylindrical wrap flag would corrupt the interpolated offsets, and TEXCOORD0..4 are
        // all in use across the three passes
        for (DWORD i = 0; i < 5; i++)
            device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(D3DRS_WRAP0 + i), 0);

        device->SetVertexDeclaration(vertexDecl);

        // SMAA_RT_METRICS sits at c44 in both stages: the shader declares globalScreenSize as
        // (w, h, 1/w, 1/h) and reads it back as .zwxy
        const float metrics[4] = { static_cast<float>(width), static_cast<float>(height), 1.0f / width, 1.0f / height };
        device->SetVertexShaderConstantF(44, metrics, 1);
        device->SetPixelShaderConstantF(44, metrics, 1);

        // s0 scene, s1 edges, s2 area, s3 search, s4 blend. Search is the one point-sampled
        // stage, none of these textures have mipmaps.
        for (DWORD s = 0; s <= 4; s++)
        {
            const DWORD filter = (s == 3) ? D3DTEXF_POINT : D3DTEXF_LINEAR;
            device->SetSamplerState(s, D3DSAMP_MINFILTER, filter);
            device->SetSamplerState(s, D3DSAMP_MAGFILTER, filter);
            device->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            device->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            device->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            device->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, 0);
        }
    }

    void Apply(IDirect3DDevice9* device)
    {
        // Never create resources or draw on an unhealthy device. Every CreateTexture would fail
        // and a transient failure would start looking permanent.
        if (device->TestCooperativeLevel() != D3D_OK)
            return;

        IDirect3DSurface9* backBuffer = nullptr;
        if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)) || backBuffer == nullptr)
            return;

        D3DSURFACE_DESC desc{};
        backBuffer->GetDesc(&desc);

        IDirect3DSurface9* sceneSurf = nullptr;
        IDirect3DSurface9* edgesSurf = nullptr;
        IDirect3DSurface9* blendSurf = nullptr;
        if (EnsureResources(device, desc.Width, desc.Height, desc.Format))
        {
            sceneCopy->GetSurfaceLevel(0, &sceneSurf);
            edgesTex->GetSurfaceLevel(0, &edgesSurf);
            blendTex->GetSurfaceLevel(0, &blendSurf);
        }

        if (sceneSurf && edgesSurf && blendSurf)
        {
            // A state block covers neither the render target nor the depth-stencil
            IDirect3DSurface9* savedRT = nullptr;
            IDirect3DSurface9* savedDS = nullptr;
            device->GetRenderTarget(0, &savedRT);
            device->GetDepthStencilSurface(&savedDS);
            stateBlock->Capture();

            // The work targets must never pair with the game's depth-stencil
            device->SetDepthStencilSurface(nullptr);

            ApplyInvariantState(device, desc.Width, desc.Height);

            // Clip-space quad with the D3D9 half-pixel offset baked in. Texcoord v = 0 is the top
            struct Vertex { float x, y, z, u, v; };
            const float ox = -1.0f / desc.Width;
            const float oy =  1.0f / desc.Height;
            const Vertex quad[4] =
            {
                { -1.0f + ox,  1.0f + oy, 0.0f, 0.0f, 0.0f },
                {  1.0f + ox,  1.0f + oy, 0.0f, 1.0f, 0.0f },
                { -1.0f + ox, -1.0f + oy, 0.0f, 0.0f, 1.0f },
                {  1.0f + ox, -1.0f + oy, 0.0f, 1.0f, 1.0f },
            };

            // Present runs outside the game's Begin/EndScene pair, so the passes get their own
            device->BeginScene();

            if (SUCCEEDED(device->StretchRect(backBuffer, nullptr, sceneSurf, nullptr, D3DTEXF_NONE)))
            {
                // Edge detection, scene -> edges. Both intermediates have to be cleared every
                // frame, alpha included. SetRenderTarget resets the viewport to the whole target
                device->SetRenderTarget(0, edgesSurf);
                device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
                device->SetVertexShader(vs[0]);
                device->SetPixelShader(ps[0]);
                device->SetTexture(0, sceneCopy);
                device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));

                // Blending-weight calculation, edges + area + search -> blend
                device->SetRenderTarget(0, blendSurf);
                device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
                device->SetVertexShader(vs[1]);
                device->SetPixelShader(ps[1]);
                device->SetTexture(1, edgesTex);
                device->SetTexture(2, areaTex);
                device->SetTexture(3, searchTex);
                device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));

                // Neighborhood blending, scene + blend -> backbuffer. Every pixel is written
                device->SetRenderTarget(0, backBuffer);
                device->SetVertexShader(vs[2]);
                device->SetPixelShader(ps[2]);
                device->SetTexture(0, sceneCopy);
                device->SetTexture(4, blendTex);
                device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
            }

            device->EndScene();

            // Render target first, then the block. SetRenderTarget resets the viewport, and the block
            // restores the game's remaining state.
            device->SetRenderTarget(0, savedRT ? savedRT : backBuffer);
            device->SetDepthStencilSurface(savedDS);
            stateBlock->Apply();

            Drop(savedRT);
            Drop(savedDS);
        }

        Drop(blendSurf);
        Drop(edgesSurf);
        Drop(sceneSurf);
        backBuffer->Release();
    }

    // Once per engine frame, so an idle menu presenting the same finished frame in a loop is
    // still filtered exactly once
    void __fastcall Present(uint8_t* self, void*, void* viewport)
    {
        auto device = *reinterpret_cast<IDirect3DDevice9**>(self + Direct3DDevice);
        if (device && !unsupported)
            Apply(device);

        shPresent.thiscall<void>(self, viewport);
    }

    // DEFAULT-pool targets and a state block alive across Reset make it fail, which would push
    // DeviceReset's Reset path back to the release-and-recreate it exists to avoid
    int __fastcall SetRes(void* self, void*, void* viewport, int newX, int newY, int fullscreen, int colorBytes, int saveSize)
    {
        ReleaseAll();
        return shSetRes.thiscall<int>(self, viewport, newX, newY, fullscreen, colorBytes, saveSize);
    }
}

FEATURE(D3DDrv, Smaa)
{
    if (!bSmaa)
        return;

    auto d3dDrv  = GetModuleHandleW(L"D3DDrv");
    auto present = GetProcAddress(d3dDrv, "?Present@UD3DRenderDevice@@UAEXPAVUViewport@@@Z");
    auto setRes  = GetProcAddress(d3dDrv, "?SetRes@UD3DRenderDevice@@UAEHPAVUViewport@@HHHHH@Z");

    if (!present || !setRes)
    {
        spdlog::error("SMAA: Present {}, SetRes {}", static_cast<void*>(present), static_cast<void*>(setRes));
        return;
    }

    shPresent = safetyhook::create_inline(present, Present);
    shSetRes  = safetyhook::create_inline(setRes, SetRes);
    spdlog::info("SMAA: enabled");
}
