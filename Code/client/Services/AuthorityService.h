#pragma once

struct World;
struct PartyStateChangedEvent;

/**
 * @brief Client-side view of local world authority.
 *
 * This initially mirrors STR's existing party-leader rule. Keeping that rule
 * behind a dedicated service lets actor ownership policy change later without
 * coupling CharacterService to social-party state.
 */
struct AuthorityService
{
    AuthorityService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~AuthorityService() noexcept = default;

    TP_NOCOPYMOVE(AuthorityService);

    [[nodiscard]] bool HasLocalActorAuthority() const noexcept;
    [[nodiscard]] bool HasLocalWorldAuthority() const noexcept;
    [[nodiscard]] bool HasWorldAuthorityGroup() const noexcept;
    [[nodiscard]] uint32_t GetWorldAuthorityPlayerId() const noexcept;

private:
    void OnPartyStateChanged(const PartyStateChangedEvent& acEvent) noexcept;
    void PublishAuthorityChanged() noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;

    entt::scoped_connection m_partyStateChangedConnection;
};
