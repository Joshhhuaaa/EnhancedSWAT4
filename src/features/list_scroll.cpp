#include "stdafx.h"
#include "common.hpp"
#include "feature.hpp"
#include "script.hpp"

namespace
{
    ptrdiff_t topOffset{};
    ptrdiff_t itemsPerPageOffset{};
    ptrdiff_t itemCountOffset{};
    bool      resolved{};

    SafetyHookInline shUpdateComponent{};

    void Resolve(void* list)
    {
        resolved = true;

        auto top          = Script::Field(list, L"Top");
        auto itemsPerPage = Script::Field(list, L"ItemsPerPage");
        auto itemCount    = Script::Field(list, L"ItemCount");

        if (!top || !itemsPerPage || !itemCount)
        {
            spdlog::error("ListScroll: GUIListBase fields not found");
            return;
        }

        topOffset          = static_cast<uint8_t*>(top)          - static_cast<uint8_t*>(list);
        itemsPerPageOffset = static_cast<uint8_t*>(itemsPerPage) - static_cast<uint8_t*>(list);
        itemCountOffset    = static_cast<uint8_t*>(itemCount)    - static_cast<uint8_t*>(list);

        spdlog::info("ListScroll: Lists no longer scroll past their last item");
    }

    void __fastcall UpdateComponent(void* self, void*, void* canvas)
    {
        shUpdateComponent.thiscall<void>(self, canvas);

        if (!resolved)
            Resolve(self);

        if (!topOffset)
            return;

        auto& top          = *reinterpret_cast<int*>(static_cast<uint8_t*>(self) + topOffset);
        auto  itemsPerPage = *reinterpret_cast<int*>(static_cast<uint8_t*>(self) + itemsPerPageOffset);
        auto  itemCount    = *reinterpret_cast<int*>(static_cast<uint8_t*>(self) + itemCountOffset);

        if (top + itemsPerPage > itemCount)
            top = std::max(0, itemCount - itemsPerPage);
    }
}

FEATURE(GUI, ListScroll)
{
    auto updateComponent = GetProcAddress(GetModuleHandleW(L"GUI"), "?UpdateComponent@UGUIVertList@@UAEXPAVUCanvas@@@Z");

    if (!updateComponent)
    {
        spdlog::error("ListScroll: UGUIVertList::UpdateComponent not found");
        return;
    }

    shUpdateComponent = safetyhook::create_inline(updateComponent, UpdateComponent);
}
