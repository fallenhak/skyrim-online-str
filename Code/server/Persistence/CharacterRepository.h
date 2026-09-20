#pragma once

#include <Persistence/CharacterRecord.h>
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
    [[nodiscard]] std::optional<CharacterRecord> GetCharacter(CharacterId aCharacterId) const;
    [[nodiscard]] std::vector<CharacterRecord> ListCharactersForOwner(std::string_view acOwnerProfileId) const;
    [[nodiscard]] bool UpdateCharacter(const CharacterRecord& acCharacter);
    [[nodiscard]] bool DeleteCharacter(CharacterId aCharacterId, std::string_view acOwnerProfileId);

private:
    Database& m_database;
};
} // namespace Persistence
