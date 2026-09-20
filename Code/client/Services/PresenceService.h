#pragma once

#include <Structs/PresencePolicy.h>

struct ConnectedEvent;
struct DisconnectedEvent;
struct CharacterWorldSyncStartedEvent;
struct NotifyPlayerList;
struct NotifyPlayerJoined;
struct NotifyPlayerLeft;
struct PresenceChangedEvent;

/**
 * @brief Tracks connection state separately from persistent-world presence.
 */
struct PresenceService
{
    explicit PresenceService(entt::dispatcher& aDispatcher) noexcept;
    ~PresenceService() noexcept = default;

    TP_NOCOPYMOVE(PresenceService);

    [[nodiscard]] bool IsConnected() const noexcept { return m_presenceState.IsConnected(); }
    [[nodiscard]] bool IsInWorld() const noexcept { return m_presenceState.IsInWorld(); }
    [[nodiscard]] uint32_t GetLocalPlayerId() const noexcept { return m_presenceState.GetLocalPlayerId(); }
    [[nodiscard]] uint32_t GetWorldAuthorityPlayerId() const noexcept;

private:
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnWorldSyncStarted(const CharacterWorldSyncStartedEvent& acEvent) noexcept;
    void OnPlayerList(const NotifyPlayerList& acMessage) noexcept;
    void OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void PublishChanged() noexcept;

    entt::dispatcher& m_dispatcher;
    ClientPresenceState m_presenceState;

    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_worldSyncStartedConnection;
    entt::scoped_connection m_playerListConnection;
    entt::scoped_connection m_playerJoinedConnection;
    entt::scoped_connection m_playerLeftConnection;
};
