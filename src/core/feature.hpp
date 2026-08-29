#pragma once

// The game loads its packages long after the ASI is in, so a fix has to wait for
// the dll it patches. Pick the module that owns the code you are hooking.
enum class GameModule
{
    Startup,
    Core,
    Engine,
    Editor,
    GUI,
    IpDrv,
    AICommon,
    SwatAIAwareness,
    SwatAICommon,
    SwatEquipment,
    SwatGame,
    SwatGui,
    Scripting,
    Tyrion,
    RWOSupport,
    IGEffectsSystem,
    IGSoundEffectsSubsystem,
    IGVisualEffectsSubsystem,
    D3DDrv,
    WinDrv,
    Window,
    ALAudio,
    RenderUtilities,
    RTCShader,
    IrrSupport,
    ScriptCompiler,
    M4D,
    DInterface,
};

using FeatureFn = void(*)();

struct Feature
{
    Feature(GameModule module, FeatureFn fn, bool editor = false);
};

void RegisterFeatures();

#define FEATURE(module, name)                                       \
    static void name();                                             \
    static Feature name##_Feature(GameModule::module, name);        \
    static void name()

// A fix that also runs under SwatEd.exe. Everything else is game only.
#define EDITOR_FEATURE(module, name)                                \
    static void name();                                             \
    static Feature name##_Feature(GameModule::module, name, true);  \
    static void name()
