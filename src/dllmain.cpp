#include "stdafx.h"
#include "common.hpp"
#include "config.hpp"
#include "feature.hpp"
#include "logging.hpp"

void Init()
{
    InitPaths();

    // SWAT 4 and TSS are separate installs of the same engine, so both executables are supported.
    // UCC, the dedicated servers and the content tools load the asi too. Bail before touching the
    // log, or whichever ran last owns it. SwatEd gets its own log and only the EDITOR_FEATURE fixes.
    if (_stricmp(sExeName.c_str(), "Swat4.exe") != 0 &&
        _stricmp(sExeName.c_str(), "Swat4X.exe") != 0 && !bEditor)
        return;

    Logging::Initialize();
    Logging::LogSystemInfo();
    Config::Read();
    RegisterFeatures();
}

CEXP void InitializeASI()
{
    std::call_once(CallbackHandler::flag, []()
    {
        CallbackHandler::RegisterCallback(Init);
    });
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        baseModule = hModule;
        InitializeASI();
    }
    return TRUE;
}
