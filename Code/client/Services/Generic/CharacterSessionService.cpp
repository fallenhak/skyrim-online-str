#include <Services/CharacterSessionService.h>

#include <Messages/NotifyCharacterList.h>
#include <Messages/NotifyCharacterSelectionResult.h>
#include <Messages/RequestCharacterList.h>
#include <Messages/SelectCharacterRequest.h>
#include <Services/TransportService.h>

CharacterSessionService::CharacterSessionService(TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept
    : m_transport(aTransport)
    , m_dispatcher(aDispatcher)
    , m_characterListConnection(aDispatcher.sink<NotifyCharacterList>().connect<&CharacterSessionService::HandleCharacterList>(this))
    , m_characterSelectionResultConnection(aDispatcher.sink<NotifyCharacterSelectionResult>().connect<&CharacterSessionService::HandleCharacterSelectionResult>(this))
{
}

bool CharacterSessionService::RequestCharacterList() const noexcept
{
    ::RequestCharacterList request{};
    return m_transport.Send(request);
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

void CharacterSessionService::HandleCharacterSelectionResult(const NotifyCharacterSelectionResult& acMessage) const noexcept
{
    m_dispatcher.trigger(CharacterSelectionResultEvent{acMessage.Status});
}
