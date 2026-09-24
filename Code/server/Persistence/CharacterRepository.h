#pragma once

#include <Persistence/CharacterRecord.h>
#include <Persistence/CharacterRuntimeState.h>
#include <Persistence/Database.h>

#include <optional>
#include <string_view>
#include <vector>

namespace Persistence
{
struct CharacterRepository final
{
    explicit CharacterRepository(Database& aDatabase) noexcept;
    ~CharacterRepository() noexcept = default;

    CharacterRepository(const CharacterRepository&) = delete;
    CharacterRepository& operator=(const CharacterRepository&) = delete;
    CharacterRepository(CharacterRepository&&) = delete;
    CharacterRepository& operator=(CharacterRepository&&) = delete;

    [[nodiscard]] CharacterId CreateCharacter(const CharacterRecord& acCharacter);
    // Ownership is enforced in the SQL query; use this for authenticated player/session access.
    [[nodiscard]] std::optional<CharacterRecord> GetCharacterForOwner(CharacterId aCharacterId, std::string_view acOwnerProfileId) const;
    [[nodiscard]] std::vector<CharacterRecord> ListCharactersForOwner(std::string_view acOwnerProfileId) const;
    [[nodiscard]] bool UpdateCharacter(const CharacterRecord& acCharacter);
    // Runtime save-back intentionally updates only location/current vitals. It must be used
    // instead of UpdateCharacter for authenticated persistent-player gameplay state.
    [[nodiscard]] bool UpdateCharacterRuntimeState(CharacterId aCharacterId, std::string_view acOwnerProfileId, const CharacterRuntimeState& acState);
    [[nodiscard]] bool DeleteCharacter(CharacterId aCharacterId, std::string_view acOwnerProfileId);

private:
    Database& m_database;
};
} // namespace Persistence
