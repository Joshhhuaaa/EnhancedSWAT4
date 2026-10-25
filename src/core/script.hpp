#pragma once

// Live read and write of UnrealScript properties by name, resolved through Core.dll's own
// reflection, so it reaches any var on any class whether or not it is config.
namespace Script
{
    // Address of a property's storage inside an object, or nullptr if the object's class
    // has no field by that name. Names a var, not a function.
    void* Field(void* object, const wchar_t* name);

    // Address of a class default property, or nullptr if the class or property does not exist.
    void* Default(const wchar_t* className, const wchar_t* name);
}
