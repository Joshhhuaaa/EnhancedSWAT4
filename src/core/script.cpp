#include "stdafx.h"
#include "script.hpp"

namespace
{
    constexpr int FNAME_Find = 0;

    // Read off Core.dll, identical in base and expansion: UObject::Class from
    // UField::GetOwnerClass, SuperField from UStruct::GetSuperStruct, Name and Next from
    // UStruct::AddCppProperty, Children from the same, UProperty::Offset from the
    // PropertiesSize accumulation in UStruct::Link.
    constexpr size_t ObjectClass    = 0x24;
    constexpr size_t FieldName      = 0x20;
    constexpr size_t FieldSuper     = 0x28;
    constexpr size_t FieldNext      = 0x2c;
    constexpr size_t StructChildren = 0x40;
    constexpr size_t PropertyOffset = 0x50;

    // UClass::Defaults, a TArray<BYTE> laid out like an instance - UObject::GlobalSetProperty
    // writes ImportText into Defaults.Data + Property->Offset and then asserts
    // "Defaults.Num()==GetPropertiesSize()" against +0x518.
    constexpr size_t ClassDefaults = 0x514;

    void* const ANY_PACKAGE = reinterpret_cast<void*>(-1);

    using FNameCtorFn        = void*(__thiscall*)(void* self, const wchar_t* name, int findType);
    using StaticClassFn      = void*(__cdecl*)();
    using IsAFn              = int(__thiscall*)(void* self, void* cls);
    using StaticFindObjectFn = void*(__cdecl*)(void* cls, void* outer, const wchar_t* name, int exact);

    FNameCtorFn        fnameCtor           = nullptr;
    StaticClassFn      propertyStaticClass = nullptr;
    StaticClassFn      classStaticClass    = nullptr;
    IsAFn              isA                 = nullptr;
    StaticFindObjectFn staticFindObject    = nullptr;

    template<typename T>
    T Read(void* base, size_t offset)
    {
        return *reinterpret_cast<T*>(static_cast<uint8_t*>(base) + offset);
    }

    bool Resolve()
    {
        if (!fnameCtor)
        {
            auto core = GetModuleHandleW(L"Core");

            // Core.dll is built /Zc:wchar_t-, so anything taking a wchar_t* mangles it as PBG
            fnameCtor           = reinterpret_cast<FNameCtorFn>(GetProcAddress(core, "??0FName@@QAE@PBGW4EFindName@@@Z"));
            propertyStaticClass = reinterpret_cast<StaticClassFn>(GetProcAddress(core, "?StaticClass@UProperty@@SAPAVUClass@@XZ"));
            classStaticClass    = reinterpret_cast<StaticClassFn>(GetProcAddress(core, "?StaticClass@UClass@@SAPAV1@XZ"));
            isA                 = reinterpret_cast<IsAFn>(GetProcAddress(core, "?IsA@UObject@@QBEHPAVUClass@@@Z"));
            staticFindObject    = reinterpret_cast<StaticFindObjectFn>(GetProcAddress(core, "?StaticFindObject@UObject@@SAPAV1@PAVUClass@@PAV1@PBGH@Z"));
        }

        return fnameCtor && propertyStaticClass && classStaticClass && isA && staticFindObject;
    }

    // Offset of a named property within instances of cls, or -1.
    int Offset(void* cls, const wchar_t* name)
    {
        if (!Resolve() || !cls)
            return -1;

        // A name the game never registered resolves to NAME_None, so an unknown one misses
        // instead of matching everything.
        int fname = 0;
        fnameCtor(&fname, name, FNAME_Find);
        if (!fname)
            return -1;

        // Children holds functions and states as well as properties, so a name that matches
        // one of those has to keep looking - +0x50 on a UFunction is not an offset.
        auto propertyClass = propertyStaticClass();

        // UObject::FindObjectField searches one class's field hash. Walk the chain so this
        // reaches inherited properties too.
        for (; cls; cls = Read<void*>(cls, FieldSuper))
            for (void* field = Read<void*>(cls, StructChildren); field; field = Read<void*>(field, FieldNext))
                if (Read<int>(field, FieldName) == fname && isA(field, propertyClass))
                    return Read<int>(field, PropertyOffset);

        return -1;
    }
}

void* Script::Field(void* object, const wchar_t* name)
{
    if (!object)
        return nullptr;

    auto offset = Offset(Read<void*>(object, ObjectClass), name);
    return offset < 0 ? nullptr : static_cast<uint8_t*>(object) + offset;
}

void* Script::Default(const wchar_t* className, const wchar_t* name)
{
    if (!Resolve())
        return nullptr;

    auto cls = staticFindObject(classStaticClass(), ANY_PACKAGE, className, 0);
    if (!cls)
        return nullptr;

    auto defaults = Read<uint8_t*>(cls, ClassDefaults);
    auto offset   = Offset(cls, name);

    return (!defaults || offset < 0) ? nullptr : defaults + offset;
}
