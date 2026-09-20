#include <Services/SessionService.h>

#include <limits>
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
    return summary;
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
    summaries.reserve(records.size());
    for (const auto& record : records)
        summaries.push_back(MakeCharacterSummary(record));

    return summaries;
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
        pSession->SelectedCharacterId.reset();
        pSession->State = SessionState::kAwaitingCharacterSelection;
        return std::nullopt;
    }

    CharacterLoadSnapshot snapshot{};
    snapshot.CharacterId = static_cast<std::uint64_t>(character->Id);
    snapshot.Name = character->Name;
    snapshot.Race = character->Race;
    snapshot.Sex = character->Sex;
    snapshot.Level = character->Level;
    snapshot.WorldSpaceId = character->WorldSpace;
    snapshot.CellId = character->Cell;
    snapshot.PositionX = character->PositionX;
    snapshot.PositionY = character->PositionY;
    snapshot.PositionZ = character->PositionZ;
    snapshot.Health = character->Health;
    snapshot.Magicka = character->Magicka;
    snapshot.Stamina = character->Stamina;

    pSession->State = SessionState::kAwaitingClientReady;
    return snapshot;
}
