#pragma once

#include "Events/ConnectedEvent.h"
#include "Events/DisconnectedEvent.h"
#include "Services/ReconnectPolicy.h"

#include <atomic>
#include <Client.hpp>

struct ImguiService;
struct UpdateEvent;
struct ClientMessage;
struct AuthenticationResponse;
struct NotifySettingsChange;
struct CharacterWorldSyncStartedEvent;

struct World;

using TiltedPhoques::Client;

/**
 * @brief Handles communication with the server.
 */
struct TransportService : Client
{
    TransportService(World& aWorld, entt::dispatcher& aDispatcher) noexcept;
    ~TransportService() noexcept = default;

    TP_NOCOPYMOVE(TransportService);

    bool Send(const ClientMessage& acMessage) const noexcept;

    /** Start or retry the session described by the launcher auth config. */
    void StartLauncherSession() noexcept;
    void RetryLauncherSession() noexcept;
    [[nodiscard]] bool HasLauncherAuthSession() const noexcept { return m_launcherConfigPresent; }
    // True from an unexpected drop out of the world until the world is entered again. The world
    // stays frozen meanwhile: no local simulation may diverge from the server.
    [[nodiscard]] bool IsResumingSession() const noexcept { return m_reconnect.IsResuming(); }

    void OnConsume(const void* apData, uint32_t aSize) override;
    void OnConnected() override;
    void OnDisconnected(EDisconnectReason aReason) override;
    void OnUpdate() override;

    [[nodiscard]] bool IsOnline() const noexcept { return m_connected; }
    void SetServerPassword(const std::string& acPassword) noexcept { m_serverPassword = acPassword; }
    const uint32_t& GetLocalPlayerId() const noexcept { return m_localPlayerId; }

protected:
    // Event handlers
    void HandleUpdate(const UpdateEvent& acEvent) noexcept;
    void HandleConnected(const ConnectedEvent& acEvent) noexcept;
    void HandleDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void HandleWorldSyncStarted(const CharacterWorldSyncStartedEvent& acEvent) noexcept;

    // Packet handlers
    void HandleAuthenticationResponse(const AuthenticationResponse& acMessage) noexcept;
    void HandleNotifySettingsChange(const NotifySettingsChange& acMessage) noexcept;

private:
    [[nodiscard]] bool CanSendMessage(const ClientMessage& acMessage) const noexcept;
    void LoadLauncherSessionConfig() noexcept;
    void ConnectLauncherSession() noexcept;
    static double Now() noexcept;

    World& m_world;
    entt::dispatcher& m_dispatcher;
    bool m_connected;
    String m_serverPassword{};
    uint32_t m_localPlayerId;
    std::string m_launcherEndpoint;
    std::string m_launcherAuthToken;
    std::string m_launcherConfigErrorKey;
    bool m_launcherConfigPresent{};
    bool m_launcherAuthenticated{};
    ReconnectPolicy m_reconnect{};
    bool m_reconnectFailureShown{};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_sendServerMessageConnection;
    entt::scoped_connection m_settingsChangeConnection;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_worldSyncStartedConnection;
    std::function<void(UniquePtr<ServerMessage>&)> m_messageHandlers[kServerOpcodeMax];
};
