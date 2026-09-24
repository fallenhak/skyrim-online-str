#pragma once

#include <Persistence/CharacterRepository.h>

#include <Structs/CharacterLoadSnapshot.h>
#include <Structs/CharacterLoadSnapshotValidation.h>
#include <Structs/CharacterCreateStatus.h>
#include <Structs/CharacterReadyStatus.h>
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
    kAwaitingPlayerAssignment,
    kInWorld
};

struct CharacterSession final
{
    ConnectionId_t ConnectionId{};
    std::optional<Persistence::OwnerProfileId> OwnerProfileId;
    SessionState State{SessionState::kConnected};
    std::optional<Persistence::CharacterId> SelectedCharacterId;
};

enum class DevelopmentCharacterBootstrapResult : std::uint8_t
{
    kCreated,
    kAlreadyExists,
    kIdentityNotReady,
    kInvalidSave
};

struct CharacterSlotConfiguration final
{
    std::uint32_t Total{3};
    std::uint32_t Unlocked{1};
};

struct CharacterCreateResult final
{
    CharacterCreateStatus Status{CharacterCreateStatus::kError};
    std::uint64_t CharacterId{};
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
    [[nodiscard]] CharacterSelectionStatus GetCharacterListFailureStatus(ConnectionId_t aConnectionId) const noexcept;
    [[nodiscard]] DevelopmentCharacterBootstrapResult CreateDevelopmentCharacterFromSaveIfEmpty(
        ConnectionId_t aConnectionId, const Persistence::CharacterRecord& acSaveCharacter);
    [[nodiscard]] CharacterSlotConfiguration GetCharacterSlotConfiguration() const noexcept { return m_characterSlots; }
    void SetCharacterSlotConfiguration(std::uint32_t aTotal, std::uint32_t aUnlocked) noexcept;
    [[nodiscard]] CharacterCreateResult CreateCharacter(ConnectionId_t aConnectionId, std::uint32_t aSlotIndex, std::string_view acName);
    [[nodiscard]] bool UpdateSelectedCharacterAppearance(ConnectionId_t aConnectionId, GameId aRace, std::int32_t aSex);
    [[nodiscard]] CharacterSelectionStatus SelectCharacter(ConnectionId_t aConnectionId, std::uint64_t aCharacterId);
    [[nodiscard]] std::optional<CharacterLoadSnapshot> PrepareCharacterLoadSnapshot(ConnectionId_t aConnectionId);
    [[nodiscard]] CharacterReadyStatus AcceptCharacterReady(ConnectionId_t aConnectionId, std::uint64_t aCharacterId, float aPositionX, float aPositionY, float aPositionZ);
    [[nodiscard]] bool CanAssignPlayer(ConnectionId_t aConnectionId) const noexcept;
    [[nodiscard]] std::optional<Persistence::CharacterRecord> GetSelectedCharacterForAssignment(ConnectionId_t aConnectionId);
    [[nodiscard]] bool CompletePlayerAssignment(ConnectionId_t aConnectionId, Persistence::CharacterId aCharacterId) noexcept;
    void ResetCharacterSelection(ConnectionId_t aConnectionId) noexcept;

private:
    [[nodiscard]] static CharacterLoadSnapshot MakeSnapshot(const Persistence::CharacterRecord& acCharacter) noexcept;
    void ResetCharacterSelection(CharacterSession& aSession) noexcept;

    Persistence::CharacterRepository& m_characterRepository;
    CharacterSlotConfiguration m_characterSlots{};
    std::unordered_map<ConnectionId_t, CharacterSession> m_sessions;
};
