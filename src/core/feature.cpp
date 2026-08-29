#include "stdafx.h"
#include "common.hpp"
#include "feature.hpp"

namespace
{
    struct Entry
    {
        GameModule module;
        FeatureFn  fn;
        bool       editor;
    };

    std::vector<Entry>& List()
    {
        static std::vector<Entry> list;
        return list;
    }

    // An empty name runs the feature immediately, which is what GameModule::Startup wants.
    const wchar_t* ModuleName(GameModule module)
    {
        switch (module)
        {
        case GameModule::Core:                     return L"Core.dll";
        case GameModule::Engine:                   return L"Engine.dll";
        case GameModule::Editor:                   return L"Editor.dll";
        case GameModule::GUI:                      return L"GUI.dll";
        case GameModule::IpDrv:                    return L"IpDrv.dll";
        case GameModule::AICommon:                 return L"AICommon.dll";
        case GameModule::SwatAIAwareness:          return L"SwatAIAwareness.dll";
        case GameModule::SwatAICommon:             return L"SwatAICommon.dll";
        case GameModule::SwatEquipment:            return L"SwatEquipment.dll";
        case GameModule::SwatGame:                 return L"SwatGame.dll";
        case GameModule::SwatGui:                  return L"SwatGui.dll";
        case GameModule::Scripting:                return L"Scripting.dll";
        case GameModule::Tyrion:                   return L"Tyrion.dll";
        case GameModule::RWOSupport:               return L"RWOSupport.dll";
        case GameModule::IGEffectsSystem:          return L"IGEffectsSystem.dll";
        case GameModule::IGSoundEffectsSubsystem:  return L"IGSoundEffectsSubsystem.dll";
        case GameModule::IGVisualEffectsSubsystem: return L"IGVisualEffectsSubsystem.dll";
        case GameModule::D3DDrv:                   return L"D3DDrv.dll";
        case GameModule::WinDrv:                   return L"WinDrv.dll";
        case GameModule::Window:                   return L"Window.dll";
        case GameModule::ALAudio:                  return L"ALAudio.dll";
        case GameModule::RenderUtilities:          return L"RenderUtilities.dll";
        case GameModule::RTCShader:                return L"RTCShader.dll";
        case GameModule::IrrSupport:               return L"IrrSupport.dll";
        case GameModule::ScriptCompiler:           return L"ScriptCompiler.dll";
        case GameModule::M4D:                      return L"m4d.dll";
        case GameModule::DInterface:               return L"dinterface.dll";
        default:                                   return L"";
        }
    }
}

Feature::Feature(GameModule module, FeatureFn fn, bool editor)
{
    List().push_back({ module, fn, editor });
}

void RegisterFeatures()
{
    // CallbackHandler keys module callbacks in a std::map, so registering one per feature
    // silently drops every fix past the first for a given package. Group by module and
    // register a single callback that runs them all.
    std::map<std::wstring, std::vector<FeatureFn>> byModule;

    for (auto& entry : List())
        if (entry.editor || !bEditor)
            byModule[ModuleName(entry.module)].push_back(entry.fn);

    for (auto& [name, fns] : byModule)
    {
        CallbackHandler::RegisterCallback(name, [fns]()
        {
            for (auto fn : fns)
                fn();
        });
    }
}
