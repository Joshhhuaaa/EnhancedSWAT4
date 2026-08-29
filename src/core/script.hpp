#pragma once

// Live read and write of UnrealScript properties by name, resolved through Core.dll's own
// reflection, so it reaches any var on any class whether or not it is config.
//
// The offsets are SWAT 4's. EnhancedRS3 has the same file with Raven Shield's, and they do
// not match - UField sits four bytes further along there.
namespace Script
{
    // Address of a property's storage inside an object, or nullptr if the object's class
    // has no field by that name. Names a var, not a function.
    void* Field(void* object, const wchar_t* name);

    // The same inside a class's own defaults - what UnrealScript writes as
    // class'Name'.default.Property. Some values live only there, with no instance to go
    // through. nullptr if the class is not loaded, which is how a class another mod adds
    // reads on a build without it.
    void* Default(const wchar_t* className, const wchar_t* name);
}
