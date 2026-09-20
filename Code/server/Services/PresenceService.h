#pragma once

#include <Structs/PresencePolicy.h>

struct World;
struct Player;
struct PlayerEnterWorldEvent;
struct PlayerLeaveEvent;

/**
 * @brief Publishes persistent-world presence independently from connection lifecycle and parties.
 */
struct PresenceService
{
    PresenceService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~PresenceService() noexcept = default;

    TP_NOCOPYMOVE(PresenceService);

private:
    void OnPlayerEnterWorld(const PlayerEnterWorldEvent& acEvent) noexcept;
    void OnPlayerLeave(const PlayerLeaveEvent& acEvent) noexcept;
    void SendPlayerJoined(const Player& acPlayer, const Player& acRecipient) const noexcept;
    void SendPlayerLeft(const Player& acPlayer, const Player& acRecipient) const noexcept;
    void BroadcastPlayerList() const noexcept;

    World& m_world;

    InWorldPresenceState m_inWorldPlayers;

    entt::scoped_connection m_playerEnterWorldConnection;
    entt::scoped_connection m_playerLeaveConnection;
};
