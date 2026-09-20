#include <Persistence/CharacterRepository.h>

#include <chrono>

namespace Persistence
{
namespace
{
constexpr std::string_view kCharacterColumns = "id, owner_profile_id, name, race_mod_id, race_base_id, sex, level, worldspace_mod_id, worldspace_base_id, "
                                               "cell_mod_id, cell_base_id, position_x, position_y, position_z, health, magicka, stamina, created_at, updated_at";

[[nodiscard]] std::int64_t GetUnixTimestamp() noexcept
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void BindGameId(Database::Statement& aStatement, const int aModIndex, const int aBaseIndex, const GameId& acGameId)
{
    aStatement.Bind(aModIndex, static_cast<std::int64_t>(acGameId.ModId));
    aStatement.Bind(aBaseIndex, static_cast<std::int64_t>(acGameId.BaseId));
}

[[nodiscard]] GameId ReadGameId(const Database::Statement& acStatement, const int aModIndex, const int aBaseIndex)
{
    return GameId(static_cast<std::uint32_t>(acStatement.ColumnInt64(aModIndex)), static_cast<std::uint32_t>(acStatement.ColumnInt64(aBaseIndex)));
}

[[nodiscard]] CharacterRecord ReadCharacter(const Database::Statement& acStatement)
{
    CharacterRecord character{};
    character.Id = acStatement.ColumnInt64(0);
    character.OwnerProfileId = acStatement.ColumnText(1);
    character.Name = acStatement.ColumnText(2);
    character.Race = ReadGameId(acStatement, 3, 4);
    character.Sex = static_cast<std::int32_t>(acStatement.ColumnInt64(5));
    character.Level = static_cast<std::int32_t>(acStatement.ColumnInt64(6));
    character.WorldSpace = ReadGameId(acStatement, 7, 8);
    character.Cell = ReadGameId(acStatement, 9, 10);
    character.PositionX = static_cast<float>(acStatement.ColumnDouble(11));
    character.PositionY = static_cast<float>(acStatement.ColumnDouble(12));
    character.PositionZ = static_cast<float>(acStatement.ColumnDouble(13));
    character.Health = static_cast<float>(acStatement.ColumnDouble(14));
    character.Magicka = static_cast<float>(acStatement.ColumnDouble(15));
    character.Stamina = static_cast<float>(acStatement.ColumnDouble(16));
    character.CreatedAt = acStatement.ColumnInt64(17);
    character.UpdatedAt = acStatement.ColumnInt64(18);
    return character;
}

void BindCharacterFields(Database::Statement& aStatement, const CharacterRecord& acCharacter, const int aFirstIndex)
{
    int index = aFirstIndex;
    aStatement.Bind(index++, acCharacter.Name);
    BindGameId(aStatement, index, index + 1, acCharacter.Race);
    index += 2;
    aStatement.Bind(index++, static_cast<std::int64_t>(acCharacter.Sex));
    aStatement.Bind(index++, static_cast<std::int64_t>(acCharacter.Level));
    BindGameId(aStatement, index, index + 1, acCharacter.WorldSpace);
    index += 2;
    BindGameId(aStatement, index, index + 1, acCharacter.Cell);
    index += 2;
    aStatement.Bind(index++, static_cast<double>(acCharacter.PositionX));
    aStatement.Bind(index++, static_cast<double>(acCharacter.PositionY));
    aStatement.Bind(index++, static_cast<double>(acCharacter.PositionZ));
    aStatement.Bind(index++, static_cast<double>(acCharacter.Health));
    aStatement.Bind(index++, static_cast<double>(acCharacter.Magicka));
    aStatement.Bind(index++, static_cast<double>(acCharacter.Stamina));
}
} // namespace

CharacterRepository::CharacterRepository(Database& aDatabase) noexcept
    : m_database(aDatabase)
{
}

CharacterId CharacterRepository::CreateCharacter(const CharacterRecord& acCharacter)
{
    Database::Transaction transaction(m_database);

    auto statement = m_database.Prepare(R"sql(
        INSERT INTO characters (
            owner_profile_id, name, race_mod_id, race_base_id, sex, level,
            worldspace_mod_id, worldspace_base_id, cell_mod_id, cell_base_id,
            position_x, position_y, position_z, health, magicka, stamina, created_at, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql");

    int index = 1;
    statement.Bind(index++, acCharacter.OwnerProfileId);
    statement.Bind(index++, acCharacter.Name);
    BindGameId(statement, index, index + 1, acCharacter.Race);
    index += 2;
    statement.Bind(index++, static_cast<std::int64_t>(acCharacter.Sex));
    statement.Bind(index++, static_cast<std::int64_t>(acCharacter.Level));
    BindGameId(statement, index, index + 1, acCharacter.WorldSpace);
    index += 2;
    BindGameId(statement, index, index + 1, acCharacter.Cell);
    index += 2;
    statement.Bind(index++, static_cast<double>(acCharacter.PositionX));
    statement.Bind(index++, static_cast<double>(acCharacter.PositionY));
    statement.Bind(index++, static_cast<double>(acCharacter.PositionZ));
    statement.Bind(index++, static_cast<double>(acCharacter.Health));
    statement.Bind(index++, static_cast<double>(acCharacter.Magicka));
    statement.Bind(index++, static_cast<double>(acCharacter.Stamina));

    const auto now = GetUnixTimestamp();
    statement.Bind(index++, now);
    statement.Bind(index++, now);
    (void)statement.Step();

    const CharacterId characterId = m_database.LastInsertRowId();
    transaction.Commit();
    return characterId;
}

std::optional<CharacterRecord> CharacterRepository::GetCharacterForOwner(const CharacterId aCharacterId, const std::string_view acOwnerProfileId) const
{
    auto statement = m_database.Prepare(std::string("SELECT ") + std::string(kCharacterColumns) + " FROM characters WHERE id = ? AND owner_profile_id = ?;");
    statement.Bind(1, aCharacterId);
    statement.Bind(2, acOwnerProfileId);

    if (!statement.Step())
        return std::nullopt;

    return ReadCharacter(statement);
}

std::vector<CharacterRecord> CharacterRepository::ListCharactersForOwner(const std::string_view acOwnerProfileId) const
{
    auto statement = m_database.Prepare(std::string("SELECT ") + std::string(kCharacterColumns) + " FROM characters WHERE owner_profile_id = ? ORDER BY id ASC;");
    statement.Bind(1, acOwnerProfileId);

    std::vector<CharacterRecord> characters;
    while (statement.Step())
        characters.push_back(ReadCharacter(statement));

    return characters;
}

bool CharacterRepository::UpdateCharacter(const CharacterRecord& acCharacter)
{
    Database::Transaction transaction(m_database);

    auto statement = m_database.Prepare(R"sql(
        UPDATE characters SET
            name = ?, race_mod_id = ?, race_base_id = ?, sex = ?, level = ?,
            worldspace_mod_id = ?, worldspace_base_id = ?, cell_mod_id = ?, cell_base_id = ?,
            position_x = ?, position_y = ?, position_z = ?, health = ?, magicka = ?, stamina = ?, updated_at = ?
        WHERE id = ? AND owner_profile_id = ?;
    )sql");

    int index = 1;
    BindCharacterFields(statement, acCharacter, index);
    index += 15;
    statement.Bind(index++, GetUnixTimestamp());
    statement.Bind(index++, acCharacter.Id);
    statement.Bind(index, acCharacter.OwnerProfileId);
    (void)statement.Step();

    const bool updated = m_database.Changes() == 1;
    transaction.Commit();
    return updated;
}

bool CharacterRepository::DeleteCharacter(const CharacterId aCharacterId, const std::string_view acOwnerProfileId)
{
    Database::Transaction transaction(m_database);

    auto statement = m_database.Prepare("DELETE FROM characters WHERE id = ? AND owner_profile_id = ?;");
    statement.Bind(1, aCharacterId);
    statement.Bind(2, acOwnerProfileId);
    (void)statement.Step();

    const bool deleted = m_database.Changes() == 1;
    transaction.Commit();
    return deleted;
}
} // namespace Persistence
