#include <TiltedOnlinePCH.h>

#include <Services/PresenceService.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/PresenceChangedEvent.h>

#include <Messages/NotifyPlayerList.h>
#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLeft.h>

PresenceService::PresenceService(entt::dispatcher& aDispatcher) noexcept
    : m_dispatcher(aDispatcher)
{
    m_connectedConnection = aDispatcher.sink<ConnectedEvent>().connect<&PresenceService::OnConnected>(this);
    m_disconnectedConnection = aDispatcher.sink<DisconnectedEvent>().connect<&PresenceService::OnDisconnected>(this);
    m_playerListConnection = aDispatcher.sink<NotifyPlayerList>().connect<&PresenceService::OnPlayerList>(this);
    m_playerJoinedConnection = aDispatcher.sink<NotifyPlayerJoined>().connect<&PresenceService::OnPlayerJoined>(this);
    m_playerLeftConnection = aDispatcher.sink<NotifyPlayerLeft>().connect<&PresenceService::OnPlayerLeft>(this);
}

uint32_t PresenceService::GetWorldAuthorityPlayerId() const noexcept
{
    if (!m_connected)
        return 0;

    uint32_t authorityId = m_localPlayerId;
    for (const uint32_t playerId : m_remotePlayerIds)
        authorityId = std::min(authorityId, playerId);

    return authorityId;
}

void PresenceService::OnConnected(const ConnectedEvent& acEvent) noexcept
{
    m_connected = true;
    m_localPlayerId = acEvent.PlayerId;
    m_remotePlayerIds.clear();
    PublishChanged();
}

void PresenceService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    m_connected = false;
    m_localPlayerId = 0;
    m_remotePlayerIds.clear();
    PublishChanged();
}

void PresenceService::OnPlayerList(const NotifyPlayerList& acMessage) noexcept
{
    m_remotePlayerIds.clear();
    for (const auto& [playerId, _] : acMessage.Players)
    {
        if (playerId != m_localPlayerId)
            m_remotePlayerIds.push_back(playerId);
    }

    PublishChanged();
}

void PresenceService::OnPlayerJoined(const NotifyPlayerJoined& acMessage) noexcept
{
    if (acMessage.PlayerId == m_localPlayerId)
        return;

    if (std::find(m_remotePlayerIds.begin(), m_remotePlayerIds.end(), acMessage.PlayerId) == m_remotePlayerIds.end())
        m_remotePlayerIds.push_back(acMessage.PlayerId);

    PublishChanged();
}

void PresenceService::OnPlayerLeft(const NotifyPlayerLeft& acMessage) noexcept
{
    std::erase(m_remotePlayerIds, acMessage.PlayerId);
    PublishChanged();
}

void PresenceService::PublishChanged() noexcept
{
    m_dispatcher.trigger(PresenceChangedEvent());
}
