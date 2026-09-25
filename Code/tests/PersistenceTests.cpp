#include <TiltedCore/Stl.hpp>

#include <Persistence/CharacterRepository.h>
#include <Persistence/WorldObjectRepository.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

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
    EXPECT_EQ(acActual.SlotIndex, acExpected.SlotIndex);
    EXPECT_EQ(acActual.NeedsRaceMenu, acExpected.NeedsRaceMenu);
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

Persistence::CharacterRuntimeState MakeRuntimeState()
{
    Persistence::CharacterRuntimeState state{};
    // An empty worldspace represents an interior and is valid for runtime save-back.
    state.WorldSpace = {};
    state.Cell = GameId(0x03, 0x00000055);
    state.PositionX = -901.5f;
    state.PositionY = 42.25f;
    state.PositionZ = 18.75f;
    state.Health = 97.0f;
    state.Magicka = 61.5f;
    state.Stamina = 88.25f;
    return state;
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
    character.SlotIndex = 0;
    const auto characterId = repository.CreateCharacter(character);
    ASSERT_GT(characterId, 0);

    const auto loadedCharacter = repository.GetCharacterForOwner(characterId, character.OwnerProfileId);
    ASSERT_TRUE(loadedCharacter.has_value());
    EXPECT_EQ(loadedCharacter->Id, characterId);
    CheckCharacterValues(character, *loadedCharacter);
    EXPECT_FALSE(repository.GetCharacterForOwner(characterId, "wrong-profile").has_value());
    EXPECT_FALSE(repository.GetCharacterForOwner(characterId, "profile-' OR 1=1 --' OR 'x'='x").has_value());

    auto secondCharacter = MakeCharacter(character.OwnerProfileId, "Second Character");
    secondCharacter.SlotIndex = 1;
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

    const auto updatedCharacter = repository.GetCharacterForOwner(characterId, character.OwnerProfileId);
    ASSERT_TRUE(updatedCharacter.has_value());
    CheckCharacterValues(character, *updatedCharacter);

    auto unauthorizedUpdate = character;
    unauthorizedUpdate.OwnerProfileId = "another-profile";
    unauthorizedUpdate.Name = "Should Not Update";
    EXPECT_FALSE(repository.UpdateCharacter(unauthorizedUpdate));
    EXPECT_FALSE(repository.DeleteCharacter(characterId, "another-profile"));

    ASSERT_TRUE(repository.DeleteCharacter(characterId, character.OwnerProfileId));
    EXPECT_FALSE(repository.GetCharacterForOwner(characterId, character.OwnerProfileId).has_value());
}

TEST(PersistenceCharacterRepository, StoresTheLookOwnerScopedAndLeavesOtherColumnsAlone)
{
    Persistence::Database database(":memory:");
    database.Migrate();
    Persistence::CharacterRepository repository(database);

    auto character = MakeCharacter("look-owner", "Lookable");
    const auto characterId = repository.CreateCharacter(character);
    ASSERT_GT(characterId, 0);

    EXPECT_FALSE(repository.GetCharacterLook(characterId, character.OwnerProfileId).has_value());
    EXPECT_FALSE(repository.UpdateCharacterLook(characterId, "another-profile", "4C4B31"));
    ASSERT_TRUE(repository.UpdateCharacterLook(characterId, character.OwnerProfileId, "4C4B31"));

    const auto look = repository.GetCharacterLook(characterId, character.OwnerProfileId);
    ASSERT_TRUE(look.has_value());
    EXPECT_EQ(*look, "4C4B31");
    EXPECT_FALSE(repository.GetCharacterLook(characterId, "another-profile").has_value());

    const auto loaded = repository.GetCharacterForOwner(characterId, character.OwnerProfileId);
    ASSERT_TRUE(loaded.has_value());
    CheckCharacterValues(character, *loaded);
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
        const auto loadedCharacter = repository.GetCharacterForOwner(characterId, "disk-profile");
        ASSERT_TRUE(loadedCharacter.has_value());
        EXPECT_EQ(loadedCharacter->Name, "Persistent Character");
        EXPECT_EQ(loadedCharacter->OwnerProfileId, "disk-profile");
    }
}

TEST(PersistenceCharacterRepository, RuntimeUpdateOnlyChangesLocationAndCurrentVitals)
{
    Persistence::Database database(":memory:");
    database.Migrate();
    Persistence::CharacterRepository repository(database);

    auto character = MakeCharacter("runtime-owner-' OR 1=1 --", "Runtime Save Character");
    const auto characterId = repository.CreateCharacter(character);
    ASSERT_GT(characterId, 0);

    const auto before = repository.GetCharacterForOwner(characterId, character.OwnerProfileId);
    ASSERT_TRUE(before.has_value());

    const auto runtimeState = MakeRuntimeState();
    ASSERT_TRUE(repository.UpdateCharacterRuntimeState(characterId, character.OwnerProfileId, runtimeState));

    const auto after = repository.GetCharacterForOwner(characterId, character.OwnerProfileId);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->Id, before->Id);
    EXPECT_EQ(after->OwnerProfileId, before->OwnerProfileId);
    EXPECT_EQ(after->Name, before->Name);
    EXPECT_EQ(after->Race, before->Race);
    EXPECT_EQ(after->Sex, before->Sex);
    EXPECT_EQ(after->Level, before->Level);
    EXPECT_EQ(after->CreatedAt, before->CreatedAt);
    EXPECT_EQ(after->WorldSpace, runtimeState.WorldSpace);
    EXPECT_EQ(after->Cell, runtimeState.Cell);
    EXPECT_FLOAT_EQ(after->PositionX, runtimeState.PositionX);
    EXPECT_FLOAT_EQ(after->PositionY, runtimeState.PositionY);
    EXPECT_FLOAT_EQ(after->PositionZ, runtimeState.PositionZ);
    EXPECT_FLOAT_EQ(after->Health, runtimeState.Health);
    EXPECT_FLOAT_EQ(after->Magicka, runtimeState.Magicka);
    EXPECT_FLOAT_EQ(after->Stamina, runtimeState.Stamina);
    EXPECT_GE(after->UpdatedAt, before->UpdatedAt);

    const auto sqlLookingWrongOwner = "runtime-owner-' OR 'x'='x";
    EXPECT_FALSE(repository.UpdateCharacterRuntimeState(characterId, sqlLookingWrongOwner, MakeRuntimeState()));
    EXPECT_FALSE(repository.UpdateCharacterRuntimeState(characterId + 1, character.OwnerProfileId, MakeRuntimeState()));
}

TEST(PersistenceCharacterRuntimeState, ValidatesOnlySafeFiniteV1RuntimeValues)
{
    const auto validState = MakeRuntimeState();
    EXPECT_TRUE(Persistence::IsValidCharacterRuntimeState(1, "owner", validState));

    auto invalidState = validState;
    invalidState.PositionX = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Persistence::IsValidCharacterRuntimeState(1, "owner", invalidState));

    invalidState = validState;
    invalidState.PositionY = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(Persistence::IsValidCharacterRuntimeState(1, "owner", invalidState));

    invalidState = validState;
    invalidState.Health = -1.f;
    EXPECT_FALSE(Persistence::IsValidCharacterRuntimeState(1, "owner", invalidState));

    EXPECT_FALSE(Persistence::IsValidCharacterRuntimeState(0, "owner", validState));
    EXPECT_FALSE(Persistence::IsValidCharacterRuntimeState(1, "", validState));

    invalidState = validState;
    invalidState.Cell = {};
    EXPECT_FALSE(Persistence::IsValidCharacterRuntimeState(1, "owner", invalidState));
}

TEST(PersistenceCharacterRuntimeState, SeparatesDeadFromInvalidState)
{
    using Persistence::CharacterRuntimeStateVerdict;

    const auto validState = MakeRuntimeState();
    EXPECT_EQ(Persistence::EvaluateCharacterRuntimeState(1, "owner", validState), CharacterRuntimeStateVerdict::Valid);

    // Skyrim drives current health below zero on death: a transient state, not corruption.
    auto state = validState;
    state.Health = -37.5f;
    EXPECT_EQ(Persistence::EvaluateCharacterRuntimeState(1, "owner", state), CharacterRuntimeStateVerdict::Dead);

    state = validState;
    state.Health = 0.f;
    EXPECT_EQ(Persistence::EvaluateCharacterRuntimeState(1, "owner", state), CharacterRuntimeStateVerdict::Valid);

    // Corruption wins over death: a dead character with a broken position is still invalid.
    state.Health = -1.f;
    state.PositionZ = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(Persistence::EvaluateCharacterRuntimeState(1, "owner", state), CharacterRuntimeStateVerdict::Invalid);

    state = validState;
    state.Magicka = -1.f;
    EXPECT_EQ(Persistence::EvaluateCharacterRuntimeState(1, "owner", state), CharacterRuntimeStateVerdict::Invalid);

    state = validState;
    state.Health = -1.f;
    EXPECT_EQ(Persistence::EvaluateCharacterRuntimeState(0, "owner", state), CharacterRuntimeStateVerdict::Invalid);
}

TEST(PersistenceCharacterRepository, EnforcesOwnerScopedSlotAndCaseInsensitiveNameUniqueness)
{
    Persistence::Database database(":memory:");
    database.Migrate();
    Persistence::CharacterRepository repository(database);

    auto first = MakeCharacter("profile-a", "Arin Stone");
    first.SlotIndex = 0;
    first.NeedsRaceMenu = true;
    const auto firstResult = repository.CreateCharacterInSlot(first);
    ASSERT_EQ(firstResult.Status, Persistence::CharacterRepositoryCreateStatus::kCreated);
    ASSERT_GT(firstResult.Id, 0);

    auto occupiedSlot = first;
    occupiedSlot.Name = "Different Name";
    EXPECT_EQ(repository.CreateCharacterInSlot(occupiedSlot).Status, Persistence::CharacterRepositoryCreateStatus::kSlotOccupied);

    auto duplicateName = first;
    duplicateName.SlotIndex = 1;
    duplicateName.Name = "arin stone";
    EXPECT_EQ(repository.CreateCharacterInSlot(duplicateName).Status, Persistence::CharacterRepositoryCreateStatus::kNameTaken);

    auto otherOwner = first;
    otherOwner.OwnerProfileId = "profile-b";
    EXPECT_EQ(repository.CreateCharacterInSlot(otherOwner).Status, Persistence::CharacterRepositoryCreateStatus::kCreated);
}

TEST(PersistenceDatabase, AssignsLegacyCharactersStableOwnerScopedSlots)
{
    Persistence::Database database(":memory:");
    database.Execute("CREATE TABLE schema_version (id INTEGER PRIMARY KEY CHECK (id = 1), version INTEGER NOT NULL);");
    database.Execute("INSERT INTO schema_version (id, version) VALUES (1, 2);");
    database.Execute("CREATE TABLE characters (id INTEGER PRIMARY KEY, owner_profile_id TEXT NOT NULL);");
    database.Execute("INSERT INTO characters (id, owner_profile_id) VALUES (9, 'owner-b'), (4, 'owner-a'), (7, 'owner-a');");

    database.Migrate();

    auto statement = database.Prepare("SELECT id, slot_index, needs_race_menu FROM characters ORDER BY owner_profile_id, id;");
    ASSERT_TRUE(statement.Step());
    EXPECT_EQ(statement.ColumnInt64(0), 4);
    EXPECT_EQ(statement.ColumnInt64(1), 0);
    EXPECT_EQ(statement.ColumnInt64(2), 0);
    ASSERT_TRUE(statement.Step());
    EXPECT_EQ(statement.ColumnInt64(0), 7);
    EXPECT_EQ(statement.ColumnInt64(1), 1);
    EXPECT_EQ(statement.ColumnInt64(2), 0);
    ASSERT_TRUE(statement.Step());
    EXPECT_EQ(statement.ColumnInt64(0), 9);
    EXPECT_EQ(statement.ColumnInt64(1), 0);
    EXPECT_EQ(statement.ColumnInt64(2), 0);
    EXPECT_FALSE(statement.Step());
}

TEST(PersistenceWorldObjectRepository, BatchesChangedWorldStateAndDeletesExpiredRows)
{
    Persistence::Database database(":memory:");
    database.Migrate();
    Persistence::WorldObjectRepository repository(database);

    Persistence::WorldObjectState door{};
    door.Id = GameId{1, 0x100};
    door.CellId = GameId{1, 0x200};
    door.WorldSpaceId = GameId{0, 0x3C};
    door.CenterCoords = GridCellCoords{10, -10};
    door.IsDoor = true;
    door.DoorStateKnown = true;
    door.DoorIsOpen = false;

    Persistence::WorldObjectState activator{};
    activator.Id = GameId{1, 0x101};
    activator.CellId = door.CellId;
    activator.WorldSpaceId = door.WorldSpaceId;
    activator.CenterCoords = door.CenterCoords;
    activator.IsActivator = true;
    activator.ActivationCount = 3;

    Persistence::WorldObjectState loot{};
    loot.Id = GameId{1, 0x102};
    loot.CellId = door.CellId;
    loot.WorldSpaceId = door.WorldSpaceId;
    loot.CenterCoords = door.CenterCoords;
    loot.IsOpenLoot = true;
    loot.IsLootTaken = true;
    loot.LootRespawnAtUnix = 2'000'000'000;

    Persistence::WorldObjectState harvest{};
    harvest.Id = GameId{1, 0x103};
    harvest.CellId = door.CellId;
    harvest.WorldSpaceId = door.WorldSpaceId;
    harvest.CenterCoords = door.CenterCoords;
    harvest.IsHarvestable = true;
    harvest.IsHarvested = true;
    harvest.HarvestRespawnAtUnix = 2'000'000'100;

    repository.EnqueueUpsert(Persistence::WorldObjectState{}); // Untouched references are not persisted.
    repository.EnqueueUpsert(door);
    repository.EnqueueUpsert(activator);
    repository.EnqueueUpsert(loot);
    repository.EnqueueUpsert(harvest);

    const auto waitForCount = [&repository](const std::size_t aCount)
    {
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            auto records = repository.LoadAll();
            if (records.size() == aCount)
                return records;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return repository.LoadAll();
    };

    const auto loaded = waitForCount(4);
    ASSERT_EQ(loaded.size(), 4u);
    const auto findById = [&loaded](const GameId& acId) -> const Persistence::WorldObjectState*
    {
        const auto it = std::find_if(loaded.begin(), loaded.end(), [&acId](const auto& acState) { return acState.Id == acId; });
        return it == loaded.end() ? nullptr : &*it;
    };

    const auto* pDoor = findById(door.Id);
    ASSERT_NE(pDoor, nullptr);
    EXPECT_TRUE(pDoor->DoorStateKnown);
    EXPECT_FALSE(pDoor->DoorIsOpen);
    const auto* pActivator = findById(activator.Id);
    ASSERT_NE(pActivator, nullptr);
    EXPECT_EQ(pActivator->ActivationCount, 3u);
    const auto* pLoot = findById(loot.Id);
    ASSERT_NE(pLoot, nullptr);
    EXPECT_EQ(pLoot->LootRespawnAtUnix, loot.LootRespawnAtUnix);
    const auto* pHarvest = findById(harvest.Id);
    ASSERT_NE(pHarvest, nullptr);
    EXPECT_EQ(pHarvest->HarvestRespawnAtUnix, harvest.HarvestRespawnAtUnix);

    repository.EnqueueDelete(door.Id, door.CellId);
    repository.EnqueueDelete(activator.Id, activator.CellId);
    repository.EnqueueDelete(loot.Id, loot.CellId);
    repository.EnqueueDelete(harvest.Id, harvest.CellId);
    EXPECT_TRUE(waitForCount(0).empty());
}
