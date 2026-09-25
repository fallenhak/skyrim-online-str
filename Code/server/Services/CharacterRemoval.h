#pragma once

#include <Messages/NotifyRemoveCharacter.h>

#include <cstdint>
#include <optional>
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

/**
 * Disconnect cleanup removes the leaving player's own character. A player that never got one
 * (launcher probe, character select) owns none: "no character" must not decay to entity 0, which
 * is a real entity, the first one created after a restart. It used to (value_or(0)) and deleted
 * whoever joined first while leaving that player bound to the destroyed entity; their later cell
 * changes then serialized it as actor 0 with ownership epoch 0 and every other client ignored them.
 */
template <class TEntity>
[[nodiscard]] constexpr bool IsDisconnectingPlayersCharacter(const std::optional<TEntity>& acCharacter, const TEntity aEntity) noexcept
{
    return acCharacter.has_value() && *acCharacter == aEntity;
}
