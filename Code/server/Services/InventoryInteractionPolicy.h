#pragma once

#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Buffer.hpp>

#include <cstdint>
#include <functional>
#include <optional>

#include <Structs/Inventory.h>

#include <algorithm>
#include <cmath>
#include <limits>

struct InventoryInteractionPolicy final
{
    [[nodiscard]] static bool HasValidItemPayload(const Inventory::Entry& acItem) noexcept
    {
        if (acItem.BaseId == GameId{} || acItem.Count == 0 || acItem.Count == std::numeric_limits<int32_t>::min())
            return false;

        if (!std::isfinite(acItem.ExtraCharge) || !std::isfinite(acItem.ExtraHealth))
            return false;

        for (const auto& effect : acItem.EnchantData.Effects)
        {
            if (!std::isfinite(effect.Magnitude) || !std::isfinite(effect.RawCost))
                return false;
        }

        return true;
    }

    [[nodiscard]] static bool CanApplyItem(const Inventory& acInventory, const Inventory::Entry& acItem) noexcept
    {
        if (!HasValidItemPayload(acItem))
            return false;

        const auto duplicate = std::find_if(
            acInventory.Entries.begin(), acInventory.Entries.end(),
            [&acItem](const Inventory::Entry& acEntry) { return acEntry.CanBeMerged(acItem); });

        if (duplicate == acInventory.Entries.end())
            return acItem.Count > 0;

        const int64_t resultingCount = static_cast<int64_t>(duplicate->Count) + static_cast<int64_t>(acItem.Count);
        return resultingCount >= 0 && resultingCount <= std::numeric_limits<int32_t>::max();
    }

    [[nodiscard]] static constexpr bool IsAuthorized(
        const bool aHasOwner,
        const bool aIsCurrentOwner,
        const bool aOwnershipEpochMatches,
        const bool aIsObject,
        const bool aHasTrustedObjectState,
        const bool aIsCharacter,
        const bool aIsPlayer,
        const bool aHasPersistentCharacter,
        const bool aIsInRange) noexcept
    {
        if (!aOwnershipEpochMatches)
            return false;

        // Object inventories are deliberately ownerless, but this branch needs
        // a trusted server baseline and sender proximity to the stored location.
        // Provisional references and arbitrary InventoryComponents are rejected.
        if (!aHasOwner)
            return aIsObject && aHasTrustedObjectState && !aIsCharacter && !aIsPlayer && !aHasPersistentCharacter && aIsInRange;

        if (aIsCurrentOwner)
            return true;

        // Non-owner inventory changes model looting/pickpocketing. Persistent
        // players are never eligible for this NPC interaction exception, even
        // if a malformed component set reports IsPlayer() as false.
        return aIsCharacter && !aIsPlayer && !aHasPersistentCharacter && aIsInRange;
    }

    [[nodiscard]] static constexpr bool CanChangeEquipment(
        const bool aIsCharacter,
        const bool aHasOwner,
        const bool aIsOwnedBySender,
        const uint32_t aRequestedEpoch,
        const uint32_t aCurrentEpoch) noexcept
    {
        return aIsCharacter && aHasOwner && aIsOwnedBySender && aRequestedEpoch != 0 && aRequestedEpoch == aCurrentEpoch;
    }

    [[nodiscard]] static constexpr bool ShouldNotifyClients(const bool aUpdateClients, const bool aIsRemoteNpcInteraction) noexcept
    {
        return aUpdateClients || aIsRemoteNpcInteraction;
    }

    [[nodiscard]] static constexpr bool ShouldRelayDrop(const bool aDrop, const bool aDropsEnabled, const bool aIsRemoteNpcInteraction) noexcept
    {
        return aDropsEnabled && aDrop && !aIsRemoteNpcInteraction;
    }
};
