#include <Services/CharacterSessionService.h>

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/CharacterSessionStateChangedEvent.h>
#include <Events/CharacterSnapshotAppliedEvent.h>
#include <Events/CharacterSnapshotApplyFailedEvent.h>
#include <Events/CharacterPlayerAssignmentStartedEvent.h>
#include <Events/CharacterWorldSyncStartedEvent.h>

#include <Messages/NotifyCharacterEnteredWorld.h>
#include <Messages/NotifyCharacterLoadSnapshot.h>
#include <Messages/NotifyCharacterReadyResult.h>
#include <Messages/CharacterReadyRequest.h>
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
    , m_characterSnapshotAppliedConnection(aDispatcher.sink<CharacterSnapshotAppliedEvent>().connect<&CharacterSessionService::HandleCharacterSnapshotApplied>(this))
    , m_characterSnapshotApplyFailedConnection(aDispatcher.sink<CharacterSnapshotApplyFailedEvent>().connect<&CharacterSessionService::HandleCharacterSnapshotApplyFailed>(this))
    , m_characterReadyResultConnection(aDispatcher.sink<NotifyCharacterReadyResult>().connect<&CharacterSessionService::HandleCharacterReadyResult>(this))
    , m_characterEnteredWorldConnection(aDispatcher.sink<NotifyCharacterEnteredWorld>().connect<&CharacterSessionService::HandleCharacterEnteredWorld>(this))
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
    SetState(ClientCharacterSessionState::kAwaitingCharacterSelection);
}

void CharacterSessionService::HandleDisconnected(const DisconnectedEvent&) noexcept
{
    m_pendingSnapshot.reset();
    SetState(ClientCharacterSessionState::kDisconnected);
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
        SetState(ClientCharacterSessionState::kCharacterSelected);

    m_dispatcher.trigger(CharacterSelectionResultEvent{acMessage.Status});
}

void CharacterSessionService::HandleCharacterLoadSnapshot(const NotifyCharacterLoadSnapshot& acMessage) noexcept
{
    m_pendingSnapshot = acMessage.Snapshot;
    SetState(ClientCharacterSessionState::kApplyingCharacter);
    m_dispatcher.trigger(CharacterLoadSnapshotReceivedEvent{acMessage.Snapshot});
}

void CharacterSessionService::HandleCharacterSnapshotApplied(const CharacterSnapshotAppliedEvent& acEvent) noexcept
{
    if (m_state != ClientCharacterSessionState::kApplyingCharacter || !m_pendingSnapshot.has_value() || m_pendingSnapshot->CharacterId != acEvent.Snapshot.CharacterId)
    {
        spdlog::warn("Ignoring an out-of-sequence character snapshot apply result.");
        return;
    }

    SetState(ClientCharacterSessionState::kAwaitingClientReady);
    CharacterReadyRequest request{};
    request.CharacterId = acEvent.Snapshot.CharacterId;
    if (!m_transport.Send(request))
        spdlog::error("Failed to send CharacterReadyRequest for character {}.", request.CharacterId);
}

void CharacterSessionService::HandleCharacterSnapshotApplyFailed(const CharacterSnapshotApplyFailedEvent& acEvent) noexcept
{
    if (m_state == ClientCharacterSessionState::kApplyingCharacter)
        SetState(ClientCharacterSessionState::kAwaitingClientReady);

    spdlog::error("Character snapshot application failed: {}", acEvent.Reason.c_str());
}

void CharacterSessionService::HandleCharacterReadyResult(const NotifyCharacterReadyResult& acMessage) noexcept
{
    if (m_state != ClientCharacterSessionState::kAwaitingClientReady && m_state != ClientCharacterSessionState::kAwaitingPlayerAssignment)
    {
        spdlog::warn("Ignoring CharacterReadyResult outside the pre-world ready/assignment states.");
        return;
    }

    if (acMessage.Status == CharacterReadyStatus::kProceed)
    {
        if (m_state != ClientCharacterSessionState::kAwaitingClientReady)
        {
            spdlog::warn("Ignoring duplicate CharacterReadyResult proceed response.");
            return;
        }

        SetState(ClientCharacterSessionState::kAwaitingPlayerAssignment);
        m_dispatcher.trigger(CharacterPlayerAssignmentStartedEvent{});
        return;
    }

    if (acMessage.Status == CharacterReadyStatus::kCharacterMismatchOrUnavailable)
    {
        m_pendingSnapshot.reset();
        SetState(ClientCharacterSessionState::kAwaitingCharacterSelection);
    }
}

void CharacterSessionService::HandleCharacterEnteredWorld(const NotifyCharacterEnteredWorld& acMessage) noexcept
{
    if (m_state != ClientCharacterSessionState::kAwaitingPlayerAssignment || !m_pendingSnapshot.has_value() || m_pendingSnapshot->CharacterId != acMessage.CharacterId)
    {
        spdlog::warn("Ignoring CharacterEnteredWorld for an unexpected character {}.", acMessage.CharacterId);
        return;
    }

    SetState(ClientCharacterSessionState::kInWorld);
    m_dispatcher.trigger(CharacterWorldSyncStartedEvent{});
}

void CharacterSessionService::SetState(const ClientCharacterSessionState aState) noexcept
{
    if (m_state == aState)
        return;

    m_state = aState;
    m_dispatcher.trigger(CharacterSessionStateChangedEvent{aState});
}
