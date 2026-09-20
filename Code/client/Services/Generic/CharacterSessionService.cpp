#include <Services/CharacterSessionService.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>

#include <Messages/NotifyCharacterLoadSnapshot.h>
#include <Messages/NotifyCharacterList.h>
#include <Messages/NotifyCharacterSelectionResult.h>
#include <Messages/RequestCharacterList.h>
#include <Messages/SelectCharacterRequest.h>
#include <Services/TransportService.h>

CharacterSessionService::CharacterSessionService(TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept
    : m_transport(aTransport)
    , m_dispatcher(aDispatcher)
    , m_connectedConnection(aDispatcher.sink<ConnectedEvent>().connect<&CharacterSessionService::HandleConnected>(this))
    , m_disconnectedConnection(aDispatcher.sink<DisconnectedEvent>().connect<&CharacterSessionService::HandleDisconnected>(this))
    , m_characterListConnection(aDispatcher.sink<NotifyCharacterList>().connect<&CharacterSessionService::HandleCharacterList>(this))
    , m_characterSelectionResultConnection(aDispatcher.sink<NotifyCharacterSelectionResult>().connect<&CharacterSessionService::HandleCharacterSelectionResult>(this))
    , m_characterLoadSnapshotConnection(aDispatcher.sink<NotifyCharacterLoadSnapshot>().connect<&CharacterSessionService::HandleCharacterLoadSnapshot>(this))
{
}

bool CharacterSessionService::RequestCharacterList() const noexcept
{
    ::RequestCharacterList request{};
    return m_transport.Send(request);
}

void CharacterSessionService::HandleConnected(const ConnectedEvent&) noexcept
{
    m_pendingSnapshot.reset();
    m_state = ClientCharacterSessionState::kAwaitingCharacterSelection;
}

void CharacterSessionService::HandleDisconnected(const DisconnectedEvent&) noexcept
{
    m_pendingSnapshot.reset();
    m_state = ClientCharacterSessionState::kDisconnected;
}

bool CharacterSessionService::SelectCharacter(const std::uint64_t aCharacterId) const noexcept
{
    ::SelectCharacterRequest request{};
    request.CharacterId = aCharacterId;
    return m_transport.Send(request);
}

void CharacterSessionService::HandleCharacterList(const NotifyCharacterList& acMessage) const noexcept
{
    m_dispatcher.trigger(CharacterListReceivedEvent{acMessage.Characters});
}

void CharacterSessionService::HandleCharacterSelectionResult(const NotifyCharacterSelectionResult& acMessage) noexcept
{
    if (acMessage.Status == CharacterSelectionStatus::kSuccess)
        m_state = ClientCharacterSessionState::kCharacterSelected;

    m_dispatcher.trigger(CharacterSelectionResultEvent{acMessage.Status});
}

void CharacterSessionService::HandleCharacterLoadSnapshot(const NotifyCharacterLoadSnapshot& acMessage) noexcept
{
    m_pendingSnapshot = acMessage.Snapshot;
    m_state = ClientCharacterSessionState::kAwaitingClientReady;
    m_dispatcher.trigger(CharacterLoadSnapshotReceivedEvent{acMessage.Snapshot});
}
