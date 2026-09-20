#pragma once

struct World;
struct PresenceChangedEvent;

/**
 * @brief Client-side view of local world authority.
 *
 * Actor ownership is server-coordinated, while world authority is selected from
 * in-world player presence rather than connection state or party leadership.
 */
struct AuthorityService
{
    AuthorityService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~AuthorityService() noexcept = default;

    TP_NOCOPYMOVE(AuthorityService);

    [[nodiscard]] bool HasLocalActorAuthority() const noexcept;
    [[nodiscard]] bool HasLocalWorldAuthority() const noexcept;
    [[nodiscard]] bool HasWorldAuthoritySource() const noexcept;
    [[nodiscard]] uint32_t GetWorldAuthorityPlayerId() const noexcept;

private:
    void OnPresenceChanged(const PresenceChangedEvent& acEvent) noexcept;
    void PublishAuthorityChanged() noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;

    entt::scoped_connection m_presenceChangedConnection;
};
