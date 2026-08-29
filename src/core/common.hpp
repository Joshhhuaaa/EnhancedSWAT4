#pragma once

inline HMODULE baseModule = nullptr;
inline std::filesystem::path sExePath;
inline std::string sExeName;
inline std::filesystem::path sAsiPath;
inline bool bEditor = false;
inline bool bExpansion = false;

void InitPaths();

namespace Memory
{
    void* ReadIAT(HMODULE callerModule, const char* targetModule, const char* targetFunction);
    bool  WriteIAT(HMODULE callerModule, const char* targetModule, const char* targetFunction, void* detour);
}
