#include "stdafx.h"
#include "common.hpp"
#include "feature.hpp"
#include "script.hpp"

static constexpr float fHUDScale     = 1.0f;    // 0 is the stock native size, 1 matches the 800x600 proportions
static constexpr bool bHUDWidescreen = true;    // Prevents the HUD from stretching on widescreen

namespace
{
    constexpr float BaseHeight = 600.0f;
    constexpr float BaseAspect = 4.0f / 3.0f;

    // UCanvas, read off the copy constructor
    constexpr ptrdiff_t CanvasFont     = 0x2c;
    constexpr ptrdiff_t CanvasOrgY     = 0x3c;
    constexpr ptrdiff_t CanvasCurX     = 0x48;
    constexpr ptrdiff_t CanvasCurY     = 0x4c;
    constexpr ptrdiff_t CanvasClipY    = 0x44;
    constexpr ptrdiff_t CanvasStyle    = 0x54;
    constexpr ptrdiff_t CanvasSizeX    = 0x64;
    constexpr ptrdiff_t CanvasSizeY    = 0x68;
    constexpr ptrdiff_t CanvasWhiteTex = 0xa8;
    constexpr ptrdiff_t CanvasViewport = 0xac;

    constexpr uint8_t  STY_Normal = 1;
    constexpr uint32_t Black      = 0xff000000;

    // Share of the scope texture's edge that is its frame, with margin. The rest of the edge is
    // the black around the lens, so trimming a little too much is invisible.
    constexpr float Frame = 8.0f / 512.0f;

    // UGUIFont, read off execGetFont
    constexpr ptrdiff_t FontFixedSize = 0x6c;   // bit 0
    constexpr ptrdiff_t FontNames     = 0x78;   // TArray<FString>
    constexpr ptrdiff_t FontFonts     = 0x84;   // TArray<UFont*>

    // UFont, read off UFont::UFont and the glyph loop in UCanvas::DrawString
    constexpr ptrdiff_t FontCharacters = 0x28;  // TArray<FFontCharacter>, 0x14 each
    constexpr ptrdiff_t CharacterVSize = 0xc;
    constexpr size_t    CharacterSize  = 0x14;

    constexpr ptrdiff_t ObjectClass = 0x24;

    struct FArray { void* Data; int Num; int Max; };
    struct FString { const wchar_t* Data; int Num; int Max; };

    // GUIReticle's fields, resolved once per class; TickSize 0 for any other component
    struct Reticle { ptrdiff_t TickSize, CenterDotSize, Images[5]; };

    SafetyHookInline shClippedStrLen{};
    SafetyHookInline shClippedPrint{};
    SafetyHookInline shWrappedPrint{};
    SafetyHookInline shWrapStringToArray{};
    SafetyHookInline shExecDrawText{};
    SafetyHookInline shDrawTile{};
    SafetyHookInline shGetFont{};
    SafetyHookInline shPreDraw{};

    const wchar_t* (__thiscall* GetName)(void*) = nullptr;
    void* (__thiscall* GetDefaultObject)(void*) = nullptr;
    void* (__cdecl* StaticLoadObject)(void* cls, void* outer, const wchar_t* name, const wchar_t* file, uint32_t flags, void* sandbox) = nullptr;

    // Each UFont the GUI hands out, as the line height of its GUIFont's 800x600 entry over its own
    std::map<void*, float> fontScale;
    std::map<void*, int>   fontHeight;

    // Canvas height seen by PreDraw, for GetFont, which is only given the width
    int screenHeight = 0;

    std::map<void*, Reticle> reticles;

    // Materials seen by DrawTile, true for the sniper scope and for the mouse cursors
    std::map<void*, bool> scopeMaterials;
    std::map<void*, bool> cursorMaterials;

    // The tick mark materials and the factor their tiles were enlarged by
    void* tickMaterials[5]{};
    float tickScale = 1.0f;

    template<typename T>
    T& At(void* base, ptrdiff_t offset)
    {
        return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
    }

    int LineHeight(void* font)
    {
        auto it = fontHeight.find(font);
        if (it != fontHeight.end())
            return it->second;

        auto& chars = At<FArray>(font, FontCharacters);
        int height = 0;

        for (int i = 0; i < chars.Num; ++i)
            height = std::max(height, At<int>(chars.Data, i * CharacterSize + CharacterVSize));

        fontHeight[font] = height;
        return height;
    }

    // Pixel fonts blur at fractional scales, so they are only scaled by whole multiples
    bool IsPixelFont(void* font)
    {
        static std::map<void*, bool> fonts;

        if (!font || !GetName)
            return false;

        auto it = fonts.find(font);
        if (it == fonts.end())
            it = fonts.emplace(font, wcsncmp(GetName(font), L"pix", 3) == 0).first;

        return it->second;
    }

    // Engine.Console loads one fixed bitmap font of its own and never goes through GUIFont, so it
    // has no ladder to correct and only the screen height matters
    bool IsConsoleFont(void* font)
    {
        static std::map<void*, bool> fonts;

        if (!font || !GetName)
            return false;

        auto it = fonts.find(font);
        if (it == fonts.end())
            it = fonts.emplace(font, wcscmp(GetName(font), L"ConsoleFont") == 0).first;

        return it->second;
    }

    float Scale(void* canvas, void* font)
    {
        if (!At<void*>(canvas, CanvasViewport))
            return 1.0f;

        if (!font)
            font = At<void*>(canvas, CanvasFont);

        auto it = fontScale.find(font);
        if (it == fontScale.end())
        {
            if (!IsConsoleFont(font))
                return 1.0f;

            it = fontScale.emplace(font, 1.0f).first;
        }

        return it->second * At<int>(canvas, CanvasSizeY) / BaseHeight * fHUDScale;
    }

    // Measured at the stock scale and scaled afterwards. The original truncates each glyph's
    // scaled width to an int while DrawString accumulates the same widths as floats, and the
    // GUI clips the draw to the measurement, so measuring scaled loses the last glyph. The width
    // rounds up to cover the draw, while the height must not, since DrawTextW skips any line that
    // does not fit its box completely and tab captions fit theirs exactly.
    void __fastcall ClippedStrLen(void* self, void*, void* font, float scaleX, float scaleY, int* xl, int* yl, const wchar_t* text)
    {
        shClippedStrLen.thiscall<void>(self, font, scaleX, scaleY, xl, yl, text);

        auto s = Scale(self, font);
        *xl = static_cast<int>(std::ceil(*xl * s));
        *yl = static_cast<int>(*yl * s);
    }

    // DrawString clips vertically in texels, so CurY and ClipY are converted before scaling and
    // restored afterwards. X cannot be handled the same way because advances accumulate in pixels.
    // The box was truncated from the scaled cell, so it rounds back up to keep the cell's last row.
    void __fastcall ClippedPrint(void* self, void*, void* font, float scaleX, float scaleY, int hotKey, const wchar_t* text)
    {
        auto s  = Scale(self, font);
        auto ys = scaleY * s;

        auto curY  = At<float>(self, CanvasCurY);
        auto clipY = At<float>(self, CanvasClipY);
        auto orgY  = At<float>(self, CanvasOrgY);

        At<float>(self, CanvasCurY)  = std::round(curY / ys);
        At<float>(self, CanvasClipY) = std::ceil(clipY / ys);
        if (curY > 0.0f)
            At<float>(self, CanvasOrgY) = orgY + curY - std::round(curY / ys);

        shClippedPrint.thiscall<void>(self, font, scaleX * s, ys, hotKey, text);

        At<float>(self, CanvasCurY)  = curY;
        At<float>(self, CanvasClipY) = clipY;
        At<float>(self, CanvasOrgY)  = orgY;
    }

    // The running line height is fed back through ScaleY once per character (base Engine
    // 0x10427123 FILD [EBP-0x2c]; FMUL [EBP+0x1c]), which is exact at 1.0, as every stock caller
    // passes, and geometric at anything else. The glyphs themselves draw from the scales directly
    // and are fine, so only the reported height is wrong. Measure again with the height scale left
    // at stock, then scale that.
    void __fastcall WrappedPrint(void* self, void*, int style, int* xl, int* yl, void* font, float scaleX, float scaleY, int center, const wchar_t* text)
    {
        auto s = Scale(self, font);
        auto x = At<float>(self, CanvasCurX);
        auto y = At<float>(self, CanvasCurY);

        shWrappedPrint.thiscall<void>(self, style, xl, yl, font, scaleX * s, scaleY * s, center, text);

        if (s == 1.0f)
            return;

        At<float>(self, CanvasCurX) = x;
        At<float>(self, CanvasCurY) = y;
        shWrappedPrint.thiscall<void>(self, 0, xl, yl, font, scaleX * s, scaleY, center, text);

        *yl = static_cast<int>(*yl * s);
        At<float>(self, CanvasCurY) = y + *yl;
    }

    // Measures unscaled glyphs against the width, so the width has to come down to match.
    void __fastcall WrapStringToArray(void* self, void*, const wchar_t* text, void* out, float width, void* font, unsigned short eol, int flag)
    {
        shWrapStringToArray.thiscall<void>(self, text, out, width / Scale(self, font), font, eol, flag);
    }

    // SEF's console displays one line of output. Its scrollback uses a 40px offset and 14px spacing
    // between entries, including blank slots.
    constexpr float HistoryGap  = 40.0f;
    constexpr float HistoryStep = 14.0f;

    void __fastcall ExecDrawText(void* self, void*, void* stack, void* result)
    {
        auto font  = At<void*>(self, CanvasFont);
        auto clipY = At<float>(self, CanvasClipY);
        auto rung  = clipY - At<float>(self, CanvasCurY);

        if (IsConsoleFont(font) && rung >= HistoryGap && std::fmod(rung - HistoryGap, HistoryStep) == 0.0f)
            At<float>(self, CanvasCurY) = clipY - rung * Scale(self, font);

        shExecDrawText.thiscall<void>(self, stack, result);
    }

    bool IsScope(void* material)
    {
        if (!bHUDWidescreen || !material)
            return false;

        auto it = scopeMaterials.find(material);
        if (it == scopeMaterials.end())
            it = scopeMaterials.emplace(material, wcscmp(GetName(material), L"SniperScopeShader") == 0).first;

        return it->second;
    }

    // Cursor scaling does not match the HUD scale at non-stock resolutions
    bool IsCursor(void* material)
    {
        if (fHUDScale <= 0.0f || !material)
            return false;

        auto it = cursorMaterials.find(material);
        if (it == cursorMaterials.end())
            it = cursorMaterials.emplace(material, wcscmp(GetName(material), L"Cursor") == 0 || wcscmp(GetName(material), L"resize") == 0).first;

        return it->second;
    }

    // RenderReticle passes TickSize as both the tile size and texel extent, so restore the
    // original texel extent after enlarging the tile
    void __fastcall DrawTile(void* self, void*, void* material, float x, float y, float xl, float yl, float u, float v, float ul, float vl, float z,
                             uint32_t color, float p0, float p1, float p2, float p3)
    {
        if (material && std::find(std::begin(tickMaterials), std::end(tickMaterials), material) != std::end(tickMaterials))
        {
            ul /= tickScale;
            vl /= tickScale;
        }
        else if (IsScope(material) && xl > yl * BaseAspect + 1.0f)
        {
            // Keep the full draw for the frame, cover the side bars just inside it, then draw the
            // texture interior into the 4:3 box
            shDrawTile.thiscall<void>(self, material, x, y, xl, yl, u, v, ul, vl, z, color, p0, p1, p2, p3);

            auto w   = yl * BaseAspect;
            auto bar = (xl - w) * 0.5f;
            auto fx  = xl * Frame;
            auto fy  = yl * Frame;
            auto bx  = w * Frame;

            auto white = At<void*>(self, CanvasWhiteTex);
            auto style = At<uint8_t>(self, CanvasStyle);

            At<uint8_t>(self, CanvasStyle) = STY_Normal;
            shDrawTile.thiscall<void>(self, white, x + fx,            y + fy, bar + bx - fx, yl - fy * 2.0f, 0.0f, 0.0f, 1.0f, 1.0f, z, Black, p0, p1, p2, p3);
            shDrawTile.thiscall<void>(self, white, x + xl - bar - bx, y + fy, bar + bx - fx, yl - fy * 2.0f, 0.0f, 0.0f, 1.0f, 1.0f, z, Black, p0, p1, p2, p3);
            At<uint8_t>(self, CanvasStyle) = style;

            shDrawTile.thiscall<void>(self, material, x + bar + bx, y + fy, w - bx * 2.0f, yl - fy * 2.0f,
                                      u + ul * Frame, v + vl * Frame, ul * (1.0f - Frame * 2.0f), vl * (1.0f - Frame * 2.0f), z, color, p0, p1, p2, p3);
            return;
        }
        else if (IsCursor(material))
        {
            // MouseCursorOffset is (0,0) for the arrow, so x/y are exact. The resize cursor's (0.25,0.25)
            auto f = At<int>(self, CanvasSizeY) / BaseHeight * fHUDScale / std::max(1, At<int>(self, CanvasSizeX) / 640);
            xl *= f;
            yl *= f;
        }

        shDrawTile.thiscall<void>(self, material, x, y, xl, yl, u, v, ul, vl, z, color, p0, p1, p2, p3);
    }

    void HookDrawTile()
    {
        if (shDrawTile)
            return;

        auto drawTile = GetProcAddress(GetModuleHandleW(L"Engine"), "?DrawTile@UCanvas@@UAEXPAVUMaterial@@MMMMMMMMMVFColor@@VFPlane@@@Z");
        GetName       = reinterpret_cast<decltype(GetName)>(GetProcAddress(GetModuleHandleW(L"Core"), "?GetName@UObject@@QBEPBGXZ"));

        if (!drawTile || !GetName)
        {
            spdlog::error("HUD: UCanvas::DrawTile {}, UObject::GetName {}", static_cast<void*>(drawTile), reinterpret_cast<void*>(GetName));
            return;
        }

        shDrawTile = safetyhook::create_inline(drawTile, DrawTile);
    }

    void* LadderFont(void* self, void* like, int i)
    {
        auto& names = At<FArray>(self, FontNames);
        auto& fonts = At<FArray>(self, FontFonts);

        auto font = i < fonts.Num ? static_cast<void**>(fonts.Data)[i] : nullptr;
        if (!font)
            font = StaticLoadObject(At<void*>(like, ObjectClass), nullptr, static_cast<FString*>(names.Data)[i].Data, nullptr, 2, nullptr);

        return font;
    }

    // Stock picks a font page by width, which can downscale text and clip line endings. Pick the
    // tallest page that fits the screen height so the draw only scales up.
    void __fastcall GetFont(void* self, void*, void* stack, void** result)
    {
        shGetFont.thiscall<void>(self, stack, result);

        auto font = *result;
        if (!font || (At<uint8_t>(self, FontFixedSize) & 1))
            return;

        // What stock picks at 800x600
        int base = std::min(1, At<FArray>(self, FontNames).Num - 1);
        if (base < 0)
            return;

        auto baseFont   = LadderFont(self, font, base);
        auto baseHeight = baseFont ? LineHeight(baseFont) : 0;
        if (baseHeight <= 0)
        {
            fontScale.try_emplace(font, 1.0f);
            return;
        }

        if (screenHeight > 0)
        {
            auto target = baseHeight * screenHeight / BaseHeight;
            font        = baseFont;

            for (int i = At<FArray>(self, FontNames).Num - 1; i > base; --i)
                if (auto page = LadderFont(self, baseFont, i); page && LineHeight(page) <= target)
                {
                    font = page;
                    break;
                }

            *result = font;
        }

        auto scale = LineHeight(font) > 0 ? static_cast<float>(baseHeight) / LineHeight(font) : 1.0f;

        if (screenHeight > 0 && IsPixelFont(font))
        {
            auto s = scale * screenHeight / BaseHeight * fHUDScale;
            scale *= std::max(1.0f, std::floor(s)) / s;
        }

        fontScale[font] = scale;
    }

    void __fastcall PreDraw(void* self, void*, void* canvas)
    {
        shPreDraw.thiscall<void>(self, canvas);

        if (At<void*>(canvas, CanvasViewport))
            screenHeight = At<int>(canvas, CanvasSizeY);

        auto cls = At<void*>(self, ObjectClass);
        auto it  = reticles.find(cls);

        if (it == reticles.end())
        {
            Reticle r{};
            const wchar_t* names[] = { L"TickSize", L"CenterDotSize", L"UpImage", L"DownImage", L"LeftImage", L"RightImage", L"CenterDotImage" };
            ptrdiff_t* offsets[]   = { &r.TickSize, &r.CenterDotSize, &r.Images[0], &r.Images[1], &r.Images[2], &r.Images[3], &r.Images[4] };

            for (size_t i = 0; i < std::size(names); ++i)
                if (auto field = Script::Field(self, names[i]))
                    *offsets[i] = static_cast<uint8_t*>(field) - static_cast<uint8_t*>(self);

            if (r.TickSize && !r.CenterDotSize)
                r.TickSize = 0;

            it = reticles.emplace(cls, r).first;
        }

        auto& r = it->second;
        if (!r.TickSize || !At<void*>(canvas, CanvasViewport))
            return;

        auto defaults = GetDefaultObject(cls);
        tickScale     = At<int>(canvas, CanvasSizeY) / BaseHeight * fHUDScale;

        At<int>(self, r.TickSize)      = static_cast<int>(std::lround(At<int>(defaults, r.TickSize)      * tickScale));
        At<int>(self, r.CenterDotSize) = static_cast<int>(std::lround(At<int>(defaults, r.CenterDotSize) * tickScale));

        for (size_t i = 0; i < std::size(tickMaterials); ++i)
            tickMaterials[i] = r.Images[i] ? At<void*>(self, r.Images[i]) : nullptr;
    }
}

FEATURE(GUI, HUDScale)
{
    if (fHUDScale <= 0.0f)
        return;

    auto engine = GetModuleHandleW(L"Engine");
    auto gui    = GetModuleHandleW(L"GUI");
    auto core   = GetModuleHandleW(L"Core");

    auto clippedStrLen     = GetProcAddress(engine, "?ClippedStrLen@UCanvas@@UAEXPAVUFont@@MMAAH1PBG@Z");
    auto clippedPrint      = GetProcAddress(engine, "?ClippedPrint@UCanvas@@UAEXPAVUFont@@MMHPBG@Z");
    auto wrappedPrint      = GetProcAddress(engine, "?WrappedPrint@UCanvas@@AAEXW4ERenderStyle@@AAH1PAVUFont@@MMHPBG@Z");
    auto wrapStringToArray = GetProcAddress(engine, "?WrapStringToArray@UCanvas@@UAEXPBGPAV?$TArray@VFString@@@@MPAVUFont@@GH@Z");
    auto drawText          = GetProcAddress(engine, "?execDrawText@UCanvas@@QAEXAAUFFrame@@QAX@Z");
    auto getFont           = GetProcAddress(gui,    "?execGetFont@UGUIFont@@QAEXAAUFFrame@@QAX@Z");
    auto preDraw           = GetProcAddress(gui,    "?PreDraw@UGUIComponent@@UAEXPAVUCanvas@@@Z");

    GetDefaultObject = reinterpret_cast<decltype(GetDefaultObject)>(GetProcAddress(core, "?GetDefaultObject@UClass@@QAEPAVUObject@@XZ"));
    StaticLoadObject = reinterpret_cast<decltype(StaticLoadObject)>(GetProcAddress(core, "?StaticLoadObject@UObject@@SAPAV1@PAVUClass@@PAV1@PBG2KPAVUPackageMap@@@Z"));

    if (!clippedStrLen || !clippedPrint || !wrappedPrint || !wrapStringToArray || !drawText || !getFont || !preDraw || !GetDefaultObject || !StaticLoadObject)
    {
        spdlog::error("HUDScale: ClippedStrLen {}, ClippedPrint {}, WrappedPrint {}, WrapStringToArray {}, DrawText {}, GetFont {}, PreDraw {}, GetDefaultObject {}, StaticLoadObject {}",
            static_cast<void*>(clippedStrLen), static_cast<void*>(clippedPrint), static_cast<void*>(wrappedPrint), static_cast<void*>(wrapStringToArray),
            static_cast<void*>(drawText), static_cast<void*>(getFont), static_cast<void*>(preDraw),
            reinterpret_cast<void*>(GetDefaultObject), reinterpret_cast<void*>(StaticLoadObject));
        return;
    }

    shClippedStrLen     = safetyhook::create_inline(clippedStrLen, ClippedStrLen);
    shClippedPrint      = safetyhook::create_inline(clippedPrint, ClippedPrint);
    shWrappedPrint      = safetyhook::create_inline(wrappedPrint, WrappedPrint);
    shWrapStringToArray = safetyhook::create_inline(wrapStringToArray, WrapStringToArray);
    shExecDrawText      = safetyhook::create_inline(drawText, ExecDrawText);
    shGetFont           = safetyhook::create_inline(getFont, GetFont);
    shPreDraw           = safetyhook::create_inline(preDraw, PreDraw);
    HookDrawTile();
    spdlog::info("HUDScale: Text and reticle sized from the screen height against 800x600, x{}", static_cast<float>(fHUDScale));
}

namespace
{
    // UGUIComponent, read off ActualLeft/ActualWidth
    constexpr ptrdiff_t ComponentController = 0x28;
    constexpr ptrdiff_t ComponentParent     = 0xac;
    constexpr ptrdiff_t ComponentWinLeft    = 0xd8;
    constexpr ptrdiff_t ComponentWinWidth   = 0xdc;
    constexpr ptrdiff_t ComponentFlags      = 0xe4;   // 1 bScaled, 2 bBoundToParent, 4 bScaleToParent
    constexpr ptrdiff_t ComponentMenuState  = 0xb0;
    constexpr ptrdiff_t ComponentStyle      = 0x118;
    constexpr ptrdiff_t ComponentClientTop  = 0x130;  // ClientBounds[1], [3] at +0x138
    constexpr ptrdiff_t StyleBorders        = 0xa8;   // TArray<sBorderOffset>, 16 bytes: Left, Right, Top, Bottom

    // BorderOffsets are "pixels at 1600x1200"
    constexpr float BorderWidth  = 1600.0f;
    constexpr float BorderHeight = 1200.0f;

    // GUIController ResolutionX/Y: +0x64/+0x68 in the base, +0x70/+0x74 in the expansion
    ptrdiff_t controllerWidth  = 0;
    ptrdiff_t controllerHeight = 0;

    constexpr uint32_t BoundToParent = 0x2;

    // Children this wide are backdrops and letterbox bars, which should keep spanning the screen
    constexpr float FullWidth = 0.9f;

    SafetyHookInline shActualLeft{};
    SafetyHookInline shActualWidth{};
    SafetyHookInline shUpdateBounds{};

    // Classes with a Reticle var are HUD pages
    std::map<void*, bool> hudPages;

    bool IsHUDPage(void* component)
    {
        if (!component)
            return false;

        auto cls = At<void*>(component, ObjectClass);
        auto it  = hudPages.find(cls);

        if (it == hudPages.end())
            it = hudPages.emplace(cls, Script::Field(component, L"Reticle") != nullptr).first;

        return it->second;
    }

    int ScreenWidth(void* component)
    {
        auto controller = At<void*>(component, ComponentController);

        if (!controllerWidth)
        {
            auto width  = Script::Field(controller, L"ResolutionX");
            auto height = Script::Field(controller, L"ResolutionY");

            if (!width || !height)
                return 0;

            controllerWidth  = static_cast<uint8_t*>(width)  - static_cast<uint8_t*>(controller);
            controllerHeight = static_cast<uint8_t*>(height) - static_cast<uint8_t*>(controller);
        }

        return At<int>(controller, controllerWidth);
    }

    // The 4:3 box's share of the screen width, 1 at 4:3 or narrower
    float Box(void* component)
    {
        auto width = static_cast<float>(ScreenWidth(component));
        if (width <= 0.0f)
            return 1.0f;

        auto height = static_cast<float>(At<int>(At<void*>(component, ComponentController), controllerHeight));
        return std::min(1.0f, height * BaseAspect / width);
    }
    
    // Temporary SEF workaround. The in-game settings menu boxes the whole page to 4:3,
    // causing its letterbox bars and backdrop to stop at 75% width. Only those images
    // are pulled back out. The tab control and its contents stay within the frame.
    bool IsScriptBoxedPage(void* page)
    {
        return page && !At<void*>(page, ComponentParent) && At<float>(page, ComponentWinLeft) == 0.0f && At<float>(page, ComponentWinWidth) < 0.999f;
    }

    bool IsImage(void* component)
    {
        static std::map<void*, bool> images;

        auto cls = At<void*>(component, ObjectClass);
        auto it  = images.find(cls);

        if (it == images.end())
            it = images.emplace(cls, GetName && wcscmp(GetName(cls), L"GUIImage") == 0).first;

        return it->second;
    }

    // How much narrower than the screen the parent is, or 0 if unchanged
    float Undo(void* component)
    {
        auto parent = At<void*>(component, ComponentParent);

        if (!parent || !(At<uint32_t>(component, ComponentFlags) & BoundToParent) || At<float>(component, ComponentWinWidth) < FullWidth)
            return 0.0f;

        if (IsHUDPage(parent))
            return Box(component);

        if (IsScriptBoxedPage(parent) && IsImage(component))
            return At<float>(parent, ComponentWinWidth);

        return 0.0f;
    }

    float __fastcall ActualWidth(void* self, void*)
    {
        auto width = shActualWidth.thiscall<float>(self);

        if (IsHUDPage(self))
            return width * Box(self);

        // Undo the parent's box for a full-width child: parent width * WinWidth, at screen width
        if (auto undo = Undo(self))
            return width / undo;

        return width;
    }

    float __fastcall ActualLeft(void* self, void*)
    {
        auto left = shActualLeft.thiscall<float>(self);

        if (IsHUDPage(self))
            return left * Box(self);

        if (auto undo = Undo(self))
            return left / undo;

        if (!(At<uint32_t>(self, ComponentFlags) & BoundToParent) || !IsHUDPage(At<void*>(self, ComponentParent)))
            return left;

        // Anchor by where the element sits in the 4:3 layout: left third stays, right third
        // moves out by the whole gap, the middle by half of it
        auto box    = Box(self);
        auto centre = At<float>(self, ComponentWinLeft) + At<float>(self, ComponentWinWidth) * 0.5f;
        auto anchor = centre < 0.4f ? 0.0f : centre > 0.6f ? 1.0f : 0.5f;
        auto screen = static_cast<float>(ScreenWidth(self));

        return left + anchor * screen * (1.0f - box);
    }

    // UpdateBounds scales every border offset by SizeX / 1600, making the vertical bounds wrong off 4:3.
    // Rescale the top and bottom offsets by height instead.
    void __fastcall UpdateBounds(void* self, void*)
    {
        shUpdateBounds.thiscall<void>(self);

        auto style = At<void*>(self, ComponentStyle);
        auto width = style ? ScreenWidth(self) : 0;
        if (!width)
            return;

        auto& borders = At<FArray>(style, StyleBorders);
        auto  state   = At<uint8_t>(self, ComponentMenuState);
        if (state >= borders.Num)
            return;

        auto height = At<int>(At<void*>(self, ComponentController), controllerHeight);
        auto border = static_cast<float*>(borders.Data) + state * 4;

        for (int i = 0; i < 2; ++i)
        {
            auto offset = border[2 + i];
            At<float>(self, ComponentClientTop + i * 8) += static_cast<int>(offset * height / BorderHeight) - static_cast<int>(offset * width / BorderWidth);
        }
    }
}

FEATURE(GUI, HUDWidescreen)
{
    if (!bHUDWidescreen)
        return;

    auto gui = GetModuleHandleW(L"GUI");

    auto actualLeft   = GetProcAddress(gui, "?ActualLeft@UGUIComponent@@UAEMXZ");
    auto actualWidth  = GetProcAddress(gui, "?ActualWidth@UGUIComponent@@UAEMXZ");
    auto updateBounds = GetProcAddress(gui, "?UpdateBounds@UGUIComponent@@UAEXXZ");

    if (!actualLeft || !actualWidth || !updateBounds)
    {
        spdlog::error("HUDWidescreen: ActualLeft {}, ActualWidth {}, UpdateBounds {}", static_cast<void*>(actualLeft), static_cast<void*>(actualWidth), static_cast<void*>(updateBounds));
        return;
    }

    shActualLeft   = safetyhook::create_inline(actualLeft, ActualLeft);
    shActualWidth  = safetyhook::create_inline(actualWidth, ActualWidth);
    shUpdateBounds = safetyhook::create_inline(updateBounds, UpdateBounds);
    HookDrawTile();
    spdlog::info("HUDWidescreen: HUD laid out in a 4:3 box and anchored to the screen edges");
}
