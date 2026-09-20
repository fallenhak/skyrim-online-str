#pragma once

struct World;
struct Player;
struct PlayerJoinEvent;
struct PlayerLeaveEvent;

/**
 * @brief Publishes connected-player presence independently from parties.
 */
struct PresenceService
{
    PresenceService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~PresenceService() noexcept = default;

    TP_NOCOPYMOVE(PresenceService);

private:
    void OnPlayerJoin(const PlayerJoinEvent& acEvent) const noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) const noexcept;
    void BroadcastPlayerList(Player* apIgnoredPlayer = nullptr) const noexcept;

    World& m_world;

    entt::scoped_connection m_playerJoinConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
