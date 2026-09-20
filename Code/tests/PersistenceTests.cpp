#include <Persistence/CharacterRepository.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace
{
Persistence::CharacterRecord MakeCharacter(std::string aOwnerProfileId, std::string aName)
{
    Persistence::CharacterRecord character{};
    character.OwnerProfileId = std::move(aOwnerProfileId);
    character.Name = std::move(aName);
    character.Race = GameId(0x01, 0x00013746);
    character.Sex = 1;
    character.Level = 12;
    character.WorldSpace = GameId(0x02, 0x0000003c);
    character.Cell = GameId(0x02, 0x0000003d);
    character.PositionX = 100.5f;
    character.PositionY = -20.25f;
    character.PositionZ = 7.75f;
    character.Health = 125.0f;
    character.Magicka = 80.0f;
    character.Stamina = 110.0f;
    return character;
}

void CheckCharacterValues(const Persistence::CharacterRecord& acExpected, const Persistence::CharacterRecord& acActual)
{
    EXPECT_EQ(acActual.OwnerProfileId, acExpected.OwnerProfileId);
    EXPECT_EQ(acActual.Name, acExpected.Name);
    EXPECT_EQ(acActual.Race, acExpected.Race);
    EXPECT_EQ(acActual.Sex, acExpected.Sex);
    EXPECT_EQ(acActual.Level, acExpected.Level);
    EXPECT_EQ(acActual.WorldSpace, acExpected.WorldSpace);
    EXPECT_EQ(acActual.Cell, acExpected.Cell);
    EXPECT_FLOAT_EQ(acActual.PositionX, acExpected.PositionX);
    EXPECT_FLOAT_EQ(acActual.PositionY, acExpected.PositionY);
    EXPECT_FLOAT_EQ(acActual.PositionZ, acExpected.PositionZ);
    EXPECT_FLOAT_EQ(acActual.Health, acExpected.Health);
    EXPECT_FLOAT_EQ(acActual.Magicka, acExpected.Magicka);
    EXPECT_FLOAT_EQ(acActual.Stamina, acExpected.Stamina);
    EXPECT_GT(acActual.CreatedAt, 0);
    EXPECT_GE(acActual.UpdatedAt, acActual.CreatedAt);
}

struct TemporaryDatabaseFile final
{
    explicit TemporaryDatabaseFile(std::filesystem::path aPath)
        : path(std::move(aPath))
    {
    }

    ~TemporaryDatabaseFile()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::remove(path.string() + "-wal", error);
        std::filesystem::remove(path.string() + "-shm", error);
    }

    std::filesystem::path path;
};
} // namespace

TEST(PersistenceCharacterRepository, InitializesAndPersistsOwnerScopedRecords)
{
    Persistence::Database database(":memory:");
    database.Migrate();
    database.Migrate();
    Persistence::CharacterRepository repository(database);

    auto character = MakeCharacter("profile-' OR 1=1 --", "O'Reilly'); DROP TABLE characters; --");
    const auto characterId = repository.CreateCharacter(character);
    ASSERT_GT(characterId, 0);

    const auto loadedCharacter = repository.GetCharacter(characterId);
    ASSERT_TRUE(loadedCharacter.has_value());
    EXPECT_EQ(loadedCharacter->Id, characterId);
    CheckCharacterValues(character, *loadedCharacter);

    auto secondCharacter = MakeCharacter(character.OwnerProfileId, "Second Character");
    secondCharacter.Level = 18;
    const auto secondCharacterId = repository.CreateCharacter(secondCharacter);

    auto otherOwnerCharacter = MakeCharacter("another-profile", "Other Owner");
    const auto otherOwnerCharacterId = repository.CreateCharacter(otherOwnerCharacter);

    const auto ownerCharacters = repository.ListCharactersForOwner(character.OwnerProfileId);
    ASSERT_EQ(ownerCharacters.size(), 2u);
    EXPECT_EQ(ownerCharacters[0].Id, characterId);
    EXPECT_EQ(ownerCharacters[1].Id, secondCharacterId);

    const auto otherOwnerCharacters = repository.ListCharactersForOwner("another-profile");
    ASSERT_EQ(otherOwnerCharacters.size(), 1u);
    EXPECT_EQ(otherOwnerCharacters[0].Id, otherOwnerCharacterId);

    character.Id = characterId;
    character.Name = "Updated O'Reilly";
    character.Level = 25;
    character.Health = 200.0f;
    ASSERT_TRUE(repository.UpdateCharacter(character));

    const auto updatedCharacter = repository.GetCharacter(characterId);
    ASSERT_TRUE(updatedCharacter.has_value());
    CheckCharacterValues(character, *updatedCharacter);

    auto unauthorizedUpdate = character;
    unauthorizedUpdate.OwnerProfileId = "another-profile";
    unauthorizedUpdate.Name = "Should Not Update";
    EXPECT_FALSE(repository.UpdateCharacter(unauthorizedUpdate));
    EXPECT_FALSE(repository.DeleteCharacter(characterId, "another-profile"));

    ASSERT_TRUE(repository.DeleteCharacter(characterId, character.OwnerProfileId));
    EXPECT_FALSE(repository.GetCharacter(characterId).has_value());
}

TEST(PersistenceCharacterRepository, PersistsRecordsAfterReopeningAnOnDiskDatabase)
{
    const auto uniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDatabaseFile temporaryDatabase(std::filesystem::temp_directory_path() / ("skyrim-online-character-persistence-" + std::to_string(uniqueSuffix) + ".db"));

    Persistence::CharacterId characterId{};
    {
        Persistence::Database database(temporaryDatabase.path);
        database.Migrate();
        Persistence::CharacterRepository repository(database);
        characterId = repository.CreateCharacter(MakeCharacter("disk-profile", "Persistent Character"));
    }

    {
        Persistence::Database database(temporaryDatabase.path);
        database.Migrate();
        Persistence::CharacterRepository repository(database);
        const auto loadedCharacter = repository.GetCharacter(characterId);
        ASSERT_TRUE(loadedCharacter.has_value());
        EXPECT_EQ(loadedCharacter->Name, "Persistent Character");
        EXPECT_EQ(loadedCharacter->OwnerProfileId, "disk-profile");
    }
}
