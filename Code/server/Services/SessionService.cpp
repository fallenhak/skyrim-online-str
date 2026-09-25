#include <Services/SessionService.h>

#include <Services/CharacterLookCodec.h>
#include <Services/CharacterNamePolicy.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <spdlog/spdlog.h>
#include <utility>

namespace
{
CharacterSummary MakeCharacterSummary(const Persistence::CharacterRecord& acCharacter)
{
    CharacterSummary summary{};
    summary.CharacterId = static_cast<std::uint64_t>(acCharacter.Id);
    summary.Name = acCharacter.Name;
    summary.Race = acCharacter.Race;
    summary.Sex = acCharacter.Sex;
    summary.Level = acCharacter.Level;
    summary.SlotIndex = static_cast<std::uint8_t>(acCharacter.SlotIndex);
    return summary;
}

CharacterLoadSnapshot MakeCharacterLoadSnapshot(const Persistence::CharacterRecord& acCharacter) noexcept
{
    CharacterLoadSnapshot snapshot{};
    snapshot.CharacterId = static_cast<std::uint64_t>(acCharacter.Id);
    snapshot.Name = acCharacter.Name;
    snapshot.Race = acCharacter.Race;
    snapshot.Sex = acCharacter.Sex;
    snapshot.Level = acCharacter.Level;
    snapshot.WorldSpaceId = acCharacter.WorldSpace;
    snapshot.CellId = acCharacter.Cell;
    snapshot.PositionX = acCharacter.PositionX;
    snapshot.PositionY = acCharacter.PositionY;
    snapshot.PositionZ = acCharacter.PositionZ;
    snapshot.Health = acCharacter.Health;
    snapshot.Magicka = acCharacter.Magicka;
    snapshot.Stamina = acCharacter.Stamina;
    snapshot.NeedsRaceMenu = acCharacter.NeedsRaceMenu;
    return snapshot;
}
} // namespace

SessionService::SessionService(Persistence::CharacterRepository& aCharacterRepository) noexcept
    : m_characterRepository(aCharacterRepository)
{
}

bool SessionService::Create(const ConnectionId_t aConnectionId)
{
    CharacterSession session{};
    session.ConnectionId = aConnectionId;
    return m_sessions.emplace(aConnectionId, std::move(session)).second;
}

bool SessionService::MarkAuthenticated(const ConnectionId_t aConnectionId) noexcept
{
    auto* pSession = Get(aConnectionId);
    if (!pSession || pSession->State != SessionState::kConnected)
        return false;

    pSession->State = SessionState::kAwaitingIdentity;
    return true;
}

bool SessionService::BindIdentity(const ConnectionId_t aConnectionId, const std::string_view acOwnerProfileId)
{
    auto* pSession = Get(aConnectionId);
    if (!pSession || pSession->State != SessionState::kAwaitingIdentity || acOwnerProfileId.empty())
        return false;

    pSession->OwnerProfileId = std::string(acOwnerProfileId);
    pSession->State = SessionState::kAwaitingCharacterSelection;
    return true;
}

bool SessionService::CanProcessGameplay(const ConnectionId_t aConnectionId) const noexcept
{
    const auto* pSession = Get(aConnectionId);
    return pSession && pSession->State == SessionState::kInWorld;
}

void SessionService::Remove(const ConnectionId_t aConnectionId) noexcept
{
    m_sessions.erase(aConnectionId);
}

const CharacterSession* SessionService::Get(const ConnectionId_t aConnectionId) const noexcept
{
    const auto it = m_sessions.find(aConnectionId);
    return it == m_sessions.end() ? nullptr : &it->second;
}

CharacterSession* SessionService::Get(const ConnectionId_t aConnectionId) noexcept
{
    const auto it = m_sessions.find(aConnectionId);
    return it == m_sessions.end() ? nullptr : &it->second;
}

std::optional<std::vector<CharacterSummary>> SessionService::ListCharacters(const ConnectionId_t aConnectionId) const
{
    const auto* pSession = Get(aConnectionId);
    if (!pSession || !pSession->OwnerProfileId.has_value() || pSession->State != SessionState::kAwaitingCharacterSelection)
        return std::nullopt;

    const auto records = m_characterRepository.ListCharactersForOwner(*pSession->OwnerProfileId);

    std::vector<CharacterSummary> summaries;
    summaries.reserve(std::min(records.size(), static_cast<std::size_t>(m_characterSlots.Total)));
    for (const auto& record : records)
    {
        if (record.SlotIndex < 0 || static_cast<std::uint32_t>(record.SlotIndex) >= m_characterSlots.Total)
            continue;
        summaries.push_back(MakeCharacterSummary(record));
    }

    return summaries;
}

CharacterSelectionStatus SessionService::GetCharacterListFailureStatus(const ConnectionId_t aConnectionId) const noexcept
{
    const auto* pSession = Get(aConnectionId);
    return !pSession || !pSession->OwnerProfileId.has_value() ? CharacterSelectionStatus::kIdentityNotReady : CharacterSelectionStatus::kInvalidState;
}

DevelopmentCharacterBootstrapResult SessionService::CreateDevelopmentCharacterFromSaveIfEmpty(
    const ConnectionId_t aConnectionId, const Persistence::CharacterRecord& acSaveCharacter)
{
    auto* pSession = Get(aConnectionId);
    if (!pSession || !pSession->OwnerProfileId.has_value() || pSession->State != SessionState::kAwaitingCharacterSelection)
        return DevelopmentCharacterBootstrapResult::kIdentityNotReady;

    if (!m_characterRepository.ListCharactersForOwner(*pSession->OwnerProfileId).empty())
        return DevelopmentCharacterBootstrapResult::kAlreadyExists;

    CharacterLoadSnapshot snapshot{};
    snapshot.CharacterId = 1;
    snapshot.Name = acSaveCharacter.Name;
    snapshot.Race = acSaveCharacter.Race;
    snapshot.Sex = acSaveCharacter.Sex;
    snapshot.Level = acSaveCharacter.Level;
    snapshot.WorldSpaceId = acSaveCharacter.WorldSpace;
    snapshot.CellId = acSaveCharacter.Cell;
    snapshot.PositionX = acSaveCharacter.PositionX;
    snapshot.PositionY = acSaveCharacter.PositionY;
    snapshot.PositionZ = acSaveCharacter.PositionZ;
    snapshot.Health = acSaveCharacter.Health;
    snapshot.Magicka = acSaveCharacter.Magicka;
    snapshot.Stamina = acSaveCharacter.Stamina;
    if (!IsCharacterLoadSnapshotValid(snapshot))
        return DevelopmentCharacterBootstrapResult::kInvalidSave;

    auto character = acSaveCharacter;
    character.OwnerProfileId = *pSession->OwnerProfileId;
    character.Id = m_characterRepository.CreateCharacter(character);
    return character.Id > 0 ? DevelopmentCharacterBootstrapResult::kCreated : DevelopmentCharacterBootstrapResult::kInvalidSave;
}

void SessionService::SetCharacterSlotConfiguration(const std::uint32_t aTotal, const std::uint32_t aUnlocked) noexcept
{
    constexpr std::uint32_t kMaximumCharacterSlots = 3;
    m_characterSlots.Total = std::clamp(aTotal, 1u, kMaximumCharacterSlots);
    m_characterSlots.Unlocked = std::min(aUnlocked, m_characterSlots.Total);
}

CharacterCreateResult SessionService::CreateCharacter(const ConnectionId_t aConnectionId, const std::uint32_t aSlotIndex, const std::string_view acName)
{
    auto* pSession = Get(aConnectionId);
    if (!pSession || !pSession->OwnerProfileId.has_value() || pSession->State != SessionState::kAwaitingCharacterSelection)
        return {CharacterCreateStatus::kError, 0};

    if (aSlotIndex >= m_characterSlots.Total || aSlotIndex >= m_characterSlots.Unlocked)
        return {CharacterCreateStatus::kSlotLocked, 0};

    if (!CharacterNamePolicy::IsValid(acName))
        return {CharacterCreateStatus::kNameInvalid, 0};

    Persistence::CharacterRecord character{};
    character.OwnerProfileId = *pSession->OwnerProfileId;
    character.Name.assign(acName.data(), acName.size());
    character.SlotIndex = static_cast<std::int32_t>(aSlotIndex);
    character.NeedsRaceMenu = true;
    character.Race = GameId(0, 0x00013746); // Skyrim.esm NordRace, replaced after RaceMenu.
    character.Sex = 0;
    character.Level = 1;
    character.WorldSpace = {}; // Interior cells have no worldspace form.
    character.Cell = GameId(0, 0x000165A7); // Skyrim.esm WhiterunTempleofKynareth.
    character.PositionX = 0.f;
    character.PositionY = 0.f;
    character.PositionZ = 0.f;
    character.Health = 100.f;
    character.Magicka = 100.f;
    character.Stamina = 100.f;

    try
    {
        const auto result = m_characterRepository.CreateCharacterInSlot(character);
        if (result.Status == Persistence::CharacterRepositoryCreateStatus::kSlotOccupied)
            return {CharacterCreateStatus::kSlotOccupied, 0};
        if (result.Status == Persistence::CharacterRepositoryCreateStatus::kNameTaken)
            return {CharacterCreateStatus::kNameTaken, 0};
        if (result.Id <= 0)
            return {CharacterCreateStatus::kError, 0};

        pSession->SelectedCharacterId = result.Id;
        pSession->State = SessionState::kCharacterSelected;
        return {CharacterCreateStatus::kSuccess, static_cast<std::uint64_t>(result.Id)};
    }
    catch (const std::exception& exception)
    {
        spdlog::error("Failed to create a character for the authenticated profile: {}", exception.what());
        return {CharacterCreateStatus::kError, 0};
    }
}

bool SessionService::UpdateSelectedCharacterAppearance(const ConnectionId_t aConnectionId, const GameId aRace, const std::int32_t aSex)
{
    auto* pSession = Get(aConnectionId);
    if (!pSession || pSession->State != SessionState::kInWorld || !pSession->OwnerProfileId.has_value() || !pSession->SelectedCharacterId.has_value() || !aRace || (aSex != 0 && aSex != 1))
        return false;

    const auto character = m_characterRepository.GetCharacterForOwner(*pSession->SelectedCharacterId, *pSession->OwnerProfileId);
    if (!character.has_value() || !character->NeedsRaceMenu)
        return false;

    return m_characterRepository.UpdateCharacterAppearance(character->Id, *pSession->OwnerProfileId, aRace, aSex);
}

bool SessionService::UpdateSelectedCharacterLook(const ConnectionId_t aConnectionId, const std::string_view acLook)
{
    auto* pSession = Get(aConnectionId);
    if (!pSession || pSession->State != SessionState::kInWorld || !pSession->OwnerProfileId.has_value() || !pSession->SelectedCharacterId.has_value() || acLook.empty())
        return false;

    return m_characterRepository.UpdateCharacterLook(*pSession->SelectedCharacterId, *pSession->OwnerProfileId, acLook);
}

std::optional<std::string> SessionService::GetSelectedCharacterLook(const ConnectionId_t aConnectionId) const
{
    const auto* pSession = Get(aConnectionId);
    if (!pSession || !pSession->OwnerProfileId.has_value() || !pSession->SelectedCharacterId.has_value())
        return std::nullopt;

    return m_characterRepository.GetCharacterLook(*pSession->SelectedCharacterId, *pSession->OwnerProfileId);
}

CharacterSelectionStatus SessionService::SelectCharacter(const ConnectionId_t aConnectionId, const std::uint64_t aCharacterId)
{
    auto* pSession = Get(aConnectionId);
    if (!pSession || !pSession->OwnerProfileId.has_value())
        return CharacterSelectionStatus::kIdentityNotReady;

    if (pSession->State != SessionState::kAwaitingCharacterSelection)
        return CharacterSelectionStatus::kInvalidState;

    if (aCharacterId == 0 || aCharacterId > static_cast<std::uint64_t>(std::numeric_limits<Persistence::CharacterId>::max()))
        return CharacterSelectionStatus::kNotFoundOrNotOwned;

    const auto character = m_characterRepository.GetCharacterForOwner(static_cast<Persistence::CharacterId>(aCharacterId), *pSession->OwnerProfileId);
    if (!character.has_value())
        return CharacterSelectionStatus::kNotFoundOrNotOwned;

    pSession->SelectedCharacterId = character->Id;
    pSession->State = SessionState::kCharacterSelected;
    return CharacterSelectionStatus::kSuccess;
}

std::optional<CharacterLoadSnapshot> SessionService::PrepareCharacterLoadSnapshot(const ConnectionId_t aConnectionId)
{
    auto* pSession = Get(aConnectionId);
    if (!pSession || !pSession->OwnerProfileId.has_value() || !pSession->SelectedCharacterId.has_value() || pSession->State != SessionState::kCharacterSelected)
        return std::nullopt;

    const auto character = m_characterRepository.GetCharacterForOwner(*pSession->SelectedCharacterId, *pSession->OwnerProfileId);
    if (!character.has_value() || character->Id <= 0)
    {
        ResetCharacterSelection(*pSession);
        return std::nullopt;
    }

    auto snapshot = MakeSnapshot(*character);
    if (const auto storedLook = m_characterRepository.GetCharacterLook(character->Id, *pSession->OwnerProfileId))
    {
        const auto bytes = CharacterLookCodec::FromHex(*storedLook);
        if (const auto look = bytes ? CharacterLookCodec::Decode(*bytes) : std::nullopt)
        {
            snapshot.AppearanceChangeFlags = look->ChangeFlags;
            snapshot.Appearance = TiltedPhoques::String(look->Appearance.data(), look->Appearance.size());
        }
        else
            spdlog::error("[Appearance] stored look of character {} is unreadable; entering with race and sex only", character->Id);
    }

    if (!IsCharacterLoadSnapshotValid(snapshot))
    {
        spdlog::error("Persistent character {} failed snapshot validation; refusing world entry.", character->Id);
        ResetCharacterSelection(*pSession);
        return std::nullopt;
    }

    pSession->State = SessionState::kAwaitingClientReady;
    return snapshot;
}

CharacterReadyStatus SessionService::AcceptCharacterReady(const ConnectionId_t aConnectionId, const std::uint64_t aCharacterId, const float aPositionX, const float aPositionY,
                                                          const float aPositionZ)
{
    auto* pSession = Get(aConnectionId);
    if (!pSession || pSession->State != SessionState::kAwaitingClientReady)
        return CharacterReadyStatus::kInvalidState;

    if (!pSession->OwnerProfileId.has_value() || !pSession->SelectedCharacterId.has_value() || aCharacterId == 0 ||
        aCharacterId != static_cast<std::uint64_t>(*pSession->SelectedCharacterId))
        return CharacterReadyStatus::kCharacterMismatchOrUnavailable;

    const auto character = m_characterRepository.GetCharacterForOwner(*pSession->SelectedCharacterId, *pSession->OwnerProfileId);
    if (!character.has_value() || character->Id <= 0 || !IsCharacterLoadSnapshotValid(MakeSnapshot(*character)))
    {
        ResetCharacterSelection(*pSession);
        return CharacterReadyStatus::kCharacterMismatchOrUnavailable;
    }

    if (character->NeedsRaceMenu && !character->WorldSpace && character->Cell == GameId(0, 0x000165A7))
    {
        if (!std::isfinite(aPositionX) || !std::isfinite(aPositionY) || !std::isfinite(aPositionZ) || std::abs(aPositionX) > 100000.f ||
            std::abs(aPositionY) > 100000.f || std::abs(aPositionZ) > 100000.f)
        {
            ResetCharacterSelection(*pSession);
            return CharacterReadyStatus::kCharacterMismatchOrUnavailable;
        }

        if (!m_characterRepository.UpdateCharacterSpawnPosition(character->Id, *pSession->OwnerProfileId, aPositionX, aPositionY, aPositionZ))
            spdlog::warn("Could not store the COC marker position for new character {}.", character->Id);
    }

    pSession->State = SessionState::kAwaitingPlayerAssignment;
    return CharacterReadyStatus::kProceed;
}

bool SessionService::CanAssignPlayer(const ConnectionId_t aConnectionId) const noexcept
{
    const auto* pSession = Get(aConnectionId);
    return pSession && pSession->State == SessionState::kAwaitingPlayerAssignment;
}

std::optional<Persistence::CharacterRecord> SessionService::GetSelectedCharacterForAssignment(const ConnectionId_t aConnectionId)
{
    auto* pSession = Get(aConnectionId);
    if (!pSession || pSession->State != SessionState::kAwaitingPlayerAssignment || !pSession->OwnerProfileId.has_value() || !pSession->SelectedCharacterId.has_value())
        return std::nullopt;

    const auto character = m_characterRepository.GetCharacterForOwner(*pSession->SelectedCharacterId, *pSession->OwnerProfileId);
    if (!character.has_value() || character->Id <= 0 || !IsCharacterLoadSnapshotValid(MakeSnapshot(*character)))
    {
        ResetCharacterSelection(*pSession);
        return std::nullopt;
    }

    return character;
}

bool SessionService::CompletePlayerAssignment(const ConnectionId_t aConnectionId, const Persistence::CharacterId aCharacterId) noexcept
{
    auto* pSession = Get(aConnectionId);
    if (!pSession || pSession->State != SessionState::kAwaitingPlayerAssignment || !pSession->OwnerProfileId.has_value() || !pSession->SelectedCharacterId.has_value() || aCharacterId <= 0)
        return false;

    if (*pSession->SelectedCharacterId != aCharacterId)
        return false;

    pSession->State = SessionState::kInWorld;
    return true;
}

void SessionService::ResetCharacterSelection(const ConnectionId_t aConnectionId) noexcept
{
    if (auto* pSession = Get(aConnectionId))
        ResetCharacterSelection(*pSession);
}

void SessionService::ResetCharacterSelection(CharacterSession& aSession) noexcept
{
    aSession.SelectedCharacterId.reset();
    aSession.State = aSession.OwnerProfileId.has_value() ? SessionState::kAwaitingCharacterSelection : SessionState::kAwaitingIdentity;
}

CharacterLoadSnapshot SessionService::MakeSnapshot(const Persistence::CharacterRecord& acCharacter) noexcept
{
    return MakeCharacterLoadSnapshot(acCharacter);
}
