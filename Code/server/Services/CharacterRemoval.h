#pragma once

#include <Messages/NotifyRemoveCharacter.h>

#include <cstdint>
#include <utility>

/**
 * The removal contract of CharacterService::OnCharacterRemoveEvent, kept free of
 * the World so it can be regression tested: the script hook runs first, every
 * connected player is told the server id is gone, and only then is the entity
 * destroyed, exactly once. Renewable encounters (W14) rely on this to make
 * actors of a retired epoch disappear on clients.
 */
template <class TPlayers, class TBeforeDestroy, class TDestroy>
void NotifyAndDestroyCharacter(const std::uint32_t aServerId, TPlayers&& aPlayers, TBeforeDestroy&& aBeforeDestroy, TDestroy&& aDestroy)
{
    std::forward<TBeforeDestroy>(aBeforeDestroy)();

    NotifyRemoveCharacter response;
    response.ServerId = aServerId;

    for (auto pPlayer : aPlayers)
        pPlayer->Send(response);

    std::forward<TDestroy>(aDestroy)();
}
