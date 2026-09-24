#include <TiltedOnlinePCH.h>

#include <Services/PresenceService.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PresenceChangedEvent.h>
#include <Events/CharacterWorldSyncStartedEvent.h>

#include <Messages/NotifyPlayerList.h>
#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLeft.h>

PresenceService::PresenceService(entt::dispatcher& aDispatcher) noexcept
    : m_dispatcher(aDispatcher)
{
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&PresenceService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&PresenceService::OnDisconnected>(this);
    m_worldSyncStartedConnection = aDispatcher.sink<CharacterWorldSyncStartedEvent>().connect<&PresenceService::OnWorldSyncStarted>(this);
    m_playerListConnection = aDispatcher.sink<NotifyPlayerList>().connect<&PresenceService::OnPlayerList>(this);
    m_playerJoinedConnection = aDispatcher.sink<NotifyPlayerJoined>().connect<&PresenceService::OnPlayerJoined>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&PresenceService::OnPlayerLeft>(this);
}

uint32_t PresenceService::GetWorldAuthorityPlayerId() const noexcept
{
    return m_presenceState.GetWorldAuthorityPlayerId();
}

void PresenceService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    m_presenceState.Connect(acEvent.PlayerId);
    PublishChanged();
}

void PresenceService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_presenceState.Disconnect();
    PublishChanged();
}

void PresenceService::OnWorldSyncStarted(const CharacterWorldSyncStartedEvent&) noexcept
{
    m_presenceState.SetInWorld(true);
    PublishChanged();
}

void PresenceService::OnPlayerList(const NotifyPlayerList& acMessage) noexcept
{
    m_presenceState.ClearRemotePlayers();
    for (const auto& [playerId, _] : acMessage.Players)
        m_presenceState.AddRemotePlayer(playerId);

    PublishChanged();
}

void PresenceService::OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept
{
    m_presenceState.AddRemotePlayer(acMessage.PlayerId);

    PublishChanged();
}

void PresenceService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    m_presenceState.RemoveRemotePlayer(acMessage.PlayerId);
    PublishChanged();
}

void PresenceService::PublishChanged() noexcept
{
    m_dispatcher.trigger(PresenceChangedEvent());
}
