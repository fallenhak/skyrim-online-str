#pragma once

#include <Persistence/CharacterRepository.h>

#include <Structs/CharacterLoadSnapshot.h>
#include <Structs/CharacterSelectionStatus.h>
#include <Structs/CharacterSummary.h>

#include <Server.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using TiltedPhoques::ConnectionId_t;

enum class SessionState : std::uint8_t
{
    kConnected = 0,
    kAwaitingIdentity,
    kAwaitingCharacterSelection,
    kCharacterSelected,
    kAwaitingClientReady,
    kInWorld
};

struct CharacterSession final
{
    ConnectionId_t ConnectionId{};
    std::optional<Persistence::OwnerProfileId> OwnerProfileId;
    SessionState State{SessionState::kConnected};
    std::optional<Persistence::CharacterId> SelectedCharacterId;
};

/**
 * @brief Owns the connection-to-verified-identity session state used by character selection.
 *
 * Identity binding is deliberately server-internal. A future verified authentication provider
 * may call BindIdentity, but no client packet is allowed to choose an OwnerProfileId.
 */
struct SessionService final
{
    explicit SessionService(Persistence::CharacterRepository& aCharacterRepository) noexcept;
    ~SessionService() noexcept = default;

    SessionService(const SessionService&) = delete;
    SessionService& operator=(const SessionService&) = delete;
    SessionService(SessionService&&) = delete;
    SessionService& operator=(SessionService&&) = delete;

    [[nodiscard]] bool Create(ConnectionId_t aConnectionId);
    [[nodiscard]] bool MarkAuthenticated(ConnectionId_t aConnectionId) noexcept;
    [[nodiscard]] bool BindIdentity(ConnectionId_t aConnectionId, std::string_view acOwnerProfileId);
    [[nodiscard]] bool CanProcessGameplay(ConnectionId_t aConnectionId) const noexcept;
    void Remove(ConnectionId_t aConnectionId) noexcept;

    [[nodiscard]] CharacterSession* Get(ConnectionId_t aConnectionId) noexcept;
    [[nodiscard]] const CharacterSession* Get(ConnectionId_t aConnectionId) const noexcept;

    [[nodiscard]] std::optional<std::vector<CharacterSummary>> ListCharacters(ConnectionId_t aConnectionId) const;
    [[nodiscard]] CharacterSelectionStatus SelectCharacter(ConnectionId_t aConnectionId, std::uint64_t aCharacterId);
    [[nodiscard]] std::optional<CharacterLoadSnapshot> PrepareCharacterLoadSnapshot(ConnectionId_t aConnectionId);

private:
    Persistence::CharacterRepository& m_characterRepository;
    std::unordered_map<ConnectionId_t, CharacterSession> m_sessions;
};
