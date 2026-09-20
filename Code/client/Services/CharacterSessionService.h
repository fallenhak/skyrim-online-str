#pragma once

#include <Events/ConnectedEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/CharacterLoadSnapshotReceivedEvent.h>
#include <Events/CharacterListReceivedEvent.h>
#include <Events/CharacterSelectionResultEvent.h>

#include <Structs/CharacterLoadSnapshot.h>

#include <entt/entt.hpp>

#include <cstdint>
#include <optional>

struct NotifyCharacterList;
struct NotifyCharacterLoadSnapshot;
struct NotifyCharacterSelectionResult;
struct TransportService;

enum class ClientCharacterSessionState : std::uint8_t
{
    kDisconnected = 0,
    kAwaitingCharacterSelection,
    kCharacterSelected,
    kAwaitingClientReady,
    kInWorld
};

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
    [[nodiscard]] bool IsGameplayActive() const noexcept { return m_state == ClientCharacterSessionState::kInWorld; }
    [[nodiscard]] ClientCharacterSessionState GetState() const noexcept { return m_state; }
    [[nodiscard]] const std::optional<CharacterLoadSnapshot>& GetPendingCharacterLoadSnapshot() const noexcept { return m_pendingSnapshot; }

private:
    void HandleConnected(const ConnectedEvent& acEvent) noexcept;
    void HandleDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void HandleCharacterList(const NotifyCharacterList& acMessage) const noexcept;
    void HandleCharacterSelectionResult(const NotifyCharacterSelectionResult& acMessage) noexcept;
    void HandleCharacterLoadSnapshot(const NotifyCharacterLoadSnapshot& acMessage) noexcept;

    TransportService& m_transport;
    entt::dispatcher& m_dispatcher;
    ClientCharacterSessionState m_state{ClientCharacterSessionState::kDisconnected};
    std::optional<CharacterLoadSnapshot> m_pendingSnapshot;
    entt::scoped_connection m_connectedConnection;
    entt::scoped_connection m_disconnectedConnection;
    entt::scoped_connection m_characterListConnection;
    entt::scoped_connection m_characterSelectionResultConnection;
    entt::scoped_connection m_characterLoadSnapshotConnection;
};
