#pragma once

#include <Persistence/CharacterRecord.h>

#include <cstdint>
#include <limits>
#include <optional>

/**
 * @brief Server facts used to authorize a persistent character as a combat observer.
 *
 * `SessionCharacterId` must come from the server-owned character session, and
 * `ServerResolvedPersistentCharacterId` must come from the resolved attacker's
 * `PersistentCharacterComponent`. Neither value is read from a hit packet.
 */
struct CombatAttackerAuthorizationInput final
{
    bool SessionIsInWorld{};
    std::optional<Persistence::CharacterId> SessionCharacterId;
    std::uint32_t AttackerServerId{};
    bool AttackerEntityExists{};
    bool AttackerIsPlayerCharacter{};
    bool OwnerExists{};
    bool SenderIsCurrentOwner{};
    std::uint32_t RequestedOwnershipEpoch{};
    std::uint32_t CurrentOwnershipEpoch{};
    std::optional<Persistence::CharacterId> ServerResolvedPersistentCharacterId;
};

/**
 * @brief Pure authorization for a persistent combat attacker identity.
 *
 * This policy does not accept or infer damage, target eligibility, or a kill.
 * The returned identity is eligible for a later server-side observation path
 * only when the sender is in-world, owns the resolved attacker at the current
 * nonzero epoch, and the attacker's persistent identity matches the active
 * character bound to that session.
 */
struct CombatAttackerAuthorizationPolicy final
{
    [[nodiscard]] static constexpr std::optional<Persistence::CharacterId> ResolveAuthorizedCharacterId(
        const CombatAttackerAuthorizationInput& acInput) noexcept
    {
        if (!acInput.SessionIsInWorld || !acInput.SessionCharacterId.has_value() || *acInput.SessionCharacterId <= 0 ||
            acInput.AttackerServerId == std::numeric_limits<std::uint32_t>::max() || !acInput.AttackerEntityExists || !acInput.AttackerIsPlayerCharacter || !acInput.OwnerExists ||
            !acInput.SenderIsCurrentOwner ||
            acInput.RequestedOwnershipEpoch == 0 || acInput.CurrentOwnershipEpoch == 0 ||
            acInput.RequestedOwnershipEpoch != acInput.CurrentOwnershipEpoch ||
            !acInput.ServerResolvedPersistentCharacterId.has_value() || *acInput.ServerResolvedPersistentCharacterId <= 0 ||
            *acInput.ServerResolvedPersistentCharacterId != *acInput.SessionCharacterId)
            return std::nullopt;

        return acInput.ServerResolvedPersistentCharacterId;
    }
};
