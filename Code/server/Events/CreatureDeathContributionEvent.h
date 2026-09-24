#pragma once

#include <Persistence/CharacterRecord.h>

#include <utility>
#include <vector>

/**
 * @brief Internal server result for the contributors to an accepted Creature death.
 *
 * The CharacterIds come from the server's validated combat contribution
 * ledger. This dispatcher event is not a network message and carries no
 * attacker connection, client damage, kill claim, or reward data.
 */
struct CreatureDeathContributionEvent final
{
    explicit CreatureDeathContributionEvent(std::vector<Persistence::CharacterId> aContributorCharacterIds) noexcept
        : ContributorCharacterIds(std::move(aContributorCharacterIds))
    {
    }

    // EnTT dispatcher events must be assignable value types.
    std::vector<Persistence::CharacterId> ContributorCharacterIds;
};
