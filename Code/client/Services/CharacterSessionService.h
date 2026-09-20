#pragma once

#include <Events/CharacterListReceivedEvent.h>
#include <Events/CharacterSelectionResultEvent.h>

#include <entt/entt.hpp>

#include <cstdint>

struct NotifyCharacterList;
struct NotifyCharacterSelectionResult;
struct TransportService;

/**
 * @brief Client-side protocol facade for character listing and selection.
 *
 * This service intentionally contains no UI, automatic selection, loading, or spawning logic.
 */
struct CharacterSessionService final
{
    CharacterSessionService(TransportService& aTransport, entt::dispatcher& aDispatcher) noexcept;
    ~CharacterSessionService() noexcept = default;

    CharacterSessionService(const CharacterSessionService&) = delete;
    CharacterSessionService& operator=(const CharacterSessionService&) = delete;
    CharacterSessionService(CharacterSessionService&&) = delete;
    CharacterSessionService& operator=(CharacterSessionService&&) = delete;

    [[nodiscard]] bool RequestCharacterList() const noexcept;
    [[nodiscard]] bool SelectCharacter(std::uint64_t aCharacterId) const noexcept;

private:
    void HandleCharacterList(const NotifyCharacterList& acMessage) const noexcept;
    void HandleCharacterSelectionResult(const NotifyCharacterSelectionResult& acMessage) const noexcept;

    TransportService& m_transport;
    entt::dispatcher& m_dispatcher;
    entt::scoped_connection m_characterListConnection;
    entt::scoped_connection m_characterSelectionResultConnection;
};
