#pragma once

#include <Structs/Factions.h>

#include <cstddef>

struct FactionAuthorityPolicy final
{
    static constexpr std::size_t kMaxFactionEntriesPerList = Factions::kMaxEntriesPerList;

    [[nodiscard]] static constexpr bool IsAuthorized(
        const bool aHasCharacter,
        const bool aHasOwner,
        const bool aIsCurrentOwner,
        const bool aIsPersistentCharacter,
        const uint32_t aCurrentOwnershipEpoch,
        const uint32_t aRequestedOwnershipEpoch) noexcept
    {
        if (aIsPersistentCharacter && (!aHasOwner || !aIsCurrentOwner))
            return false;

        return aHasCharacter && aHasOwner && aIsCurrentOwner && aRequestedOwnershipEpoch != 0 && aRequestedOwnershipEpoch == aCurrentOwnershipEpoch;
    }

    [[nodiscard]] static bool HasValidPayload(const Factions& acFactions) noexcept
    {
        return HasValidEntries(acFactions.NpcFactions) && HasValidEntries(acFactions.ExtraFactions);
    }

private:
    template <class TEntries> [[nodiscard]] static bool HasValidEntries(const TEntries& acEntries) noexcept
    {
        if (acEntries.size() > kMaxFactionEntriesPerList)
            return false;

        for (std::size_t i = 0; i < acEntries.size(); ++i)
        {
            if (!acEntries[i].Id)
                return false;

            for (std::size_t j = 0; j < i; ++j)
            {
                if (acEntries[i].Id == acEntries[j].Id)
                    return false;
            }
        }

        return true;
    }
};
