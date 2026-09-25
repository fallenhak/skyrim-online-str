#pragma once

#include <Games/Skyrim/TESObjectREFR.h>

namespace ObjectSyncPolicy
{
// Only stable, plugin-placed world items have ids that can be matched on peers.
// Ingredients use the separate harvest/respawn state; player drops are temporary.
// Books remain outside this path because their read activation is intentionally unsynced.
inline bool IsOpenLootFormType(const FormType aType) noexcept
{
    switch (aType)
    {
    case FormType::Armor:
    case FormType::Light:
    case FormType::Misc:
    case FormType::Apparatus:
    case FormType::Weapon:
    case FormType::Ammo:
    case FormType::Key:
    case FormType::Alchemy:
    case FormType::Scroll:
    case FormType::SoulGem:
    case FormType::Book: // taken through PickUpObject; reading alone does not remove it
        return true;
    default:
        return false;
    }
}

inline bool IsOpenLootObject(const TESObjectREFR* apObject) noexcept
{
    return apObject && apObject->baseForm && !apObject->IsTemporary() && IsOpenLootFormType(apObject->baseForm->formType);
}

inline bool ShouldTrackLocalHarvest(
    const bool aIsHarvestable,
    const bool aIsLocalActivator,
    const bool aWasAlreadyDisabled) noexcept
{
    return aIsHarvestable && aIsLocalActivator && !aWasAlreadyDisabled;
}
} // namespace ObjectSyncPolicy
