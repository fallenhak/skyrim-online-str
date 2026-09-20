#pragma once

struct ConnectedEvent;
struct DisconnectedEvent;
struct NotifyPlayerList;
struct NotifyPlayerJoined;
struct NotifyPlayerLeft;
struct PresenceChangedEvent;

/**
 * @brief Tracks connected-player presence independently from parties.
 */
struct PresenceService
{
    explicit PresenceService(entt::dispatcher& aDispatcher) noexcept;
    ~PresenceService() noexcept = default;

    TP_NOCOPYMOVE(PresenceService);

    [[nodiscard]] bool IsConnected() const noexcept { return m_connected; }
    [[nodiscard]] uint32_t GetLocalPlayerId() const noexcept { return m_localPlayerId; }
    [[nodiscard]] uint32_t GetWorldAuthorityPlayerId() const noexcept;

private:
    void OnConnected(const ConnectedEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnPlayerList(const NotifyPlayerList& acMessage) noexcept;
    void OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept;
    void OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept;
    void PublishChanged() noexcept;

    entt::dispatcher& m_dispatcher;
    bool m_connected{false};
    uint32_t m_localPlayerId{0};
    Vector<uint32_t> m_remotePlayerIds;

    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_playerListConnection;
    entt::scoped_connection m_playerJoinedConnection;
    entt::scoped_connection m_playerLeftConnection;
};
