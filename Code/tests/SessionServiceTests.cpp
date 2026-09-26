#include <Services/SessionService.h>
#include <Services/DevelopmentSaveFormId.h>

#include <Persistence/Database.h>

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <utility>

namespace
{
Persistence::CharacterRecord MakeCharacter(std::string aOwner, std::string aName, const int aLevel)
{
    Persistence::CharacterRecord character{};
    character.OwnerProfileId = std::move(aOwner);
    character.Name = std::move(aName);
    character.Race = GameId(0x01, 0x00013746);
    character.Sex = 1;
    character.Level = aLevel;
    return character;
}
} // namespace

class SessionServiceTest : public ::testing::Test
{
protected:
    SessionServiceTest()
        : database(":memory:")
        , repository(database)
        , sessions(repository)
    {
        database.Migrate();
    }

    Persistence::Database database;
    Persistence::CharacterRepository repository;
    SessionService sessions;
};

TEST(DevelopmentSaveFormId, ResolvesStandardAndLightFormsThroughSubmittedModOrder)
{
    Mods userMods{};
    userMods.ModList.push_back({"Standard.esp", 1, false});
    userMods.ModList.push_back({"Light.esp", 4, true});
    TiltedPhoques::Vector<std::uint16_t> serverModIds{7, 12};

    EXPECT_EQ(ResolveDevelopmentSaveFormId(0x01012345, userMods, serverModIds), GameId(7, 0x0012345));
    EXPECT_EQ(ResolveDevelopmentSaveFormId(0xFE004ABC, userMods, serverModIds), GameId(12, 0xABC));
}

TEST(DevelopmentSaveFormId, RejectsMissingOrMismatchedLocalMods)
{
    Mods userMods{};
    userMods.ModList.push_back({"Standard.esp", 1, false});
    TiltedPhoques::Vector<std::uint16_t> serverModIds{7};

    EXPECT_EQ(ResolveDevelopmentSaveFormId(0, userMods, serverModIds), GameId{});
    EXPECT_EQ(ResolveDevelopmentSaveFormId(0x02012345, userMods, serverModIds), GameId{});
    EXPECT_EQ(ResolveDevelopmentSaveFormId(0xFE004ABC, userMods, serverModIds), GameId{});
}

TEST_F(SessionServiceTest, FindsTheOlderConnectionOfAReconnectingOwner)
{
    ASSERT_TRUE(sessions.Create(1));
    ASSERT_TRUE(sessions.Create(2));
    ASSERT_TRUE(sessions.Create(3));
    for (const TiltedPhoques::ConnectionId_t id : {1u, 2u, 3u})
        ASSERT_TRUE(sessions.MarkAuthenticated(id));
    ASSERT_TRUE(sessions.BindIdentity(1, "discord:1"));
    ASSERT_TRUE(sessions.BindIdentity(2, "discord:2"));

    EXPECT_EQ(sessions.FindOtherConnectionsOfOwner(3, "discord:1"), std::vector<TiltedPhoques::ConnectionId_t>{1});
    EXPECT_TRUE(sessions.FindOtherConnectionsOfOwner(1, "discord:1").empty());
    EXPECT_TRUE(sessions.FindOtherConnectionsOfOwner(3, "discord:9").empty());
}

TEST_F(SessionServiceTest, StartsWithoutIdentityAndBindsVerifiedOwner)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 101;
    ASSERT_TRUE(sessions.Create(connectionId));

    const auto* session = sessions.Get(connectionId);
    ASSERT_NE(session, nullptr);
    EXPECT_FALSE(session->OwnerProfileId.has_value());
    EXPECT_EQ(session->State, SessionState::kConnected);

    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    EXPECT_EQ(sessions.Get(connectionId)->State, SessionState::kAwaitingIdentity);
    EXPECT_TRUE(sessions.BindIdentity(connectionId, "owner-profile"));
    EXPECT_EQ(sessions.Get(connectionId)->State, SessionState::kAwaitingCharacterSelection);
    ASSERT_TRUE(sessions.Get(connectionId)->OwnerProfileId.has_value());
    EXPECT_EQ(*sessions.Get(connectionId)->OwnerProfileId, "owner-profile");
}

TEST_F(SessionServiceTest, UsesThreeSlotsWithOneUnlockedAndCreatesTempleCharacter)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 118;
    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    ASSERT_TRUE(sessions.BindIdentity(connectionId, "new-player"));

    const auto slots = sessions.GetCharacterSlotConfiguration();
    EXPECT_EQ(slots.Total, 3u);
    EXPECT_EQ(slots.Unlocked, 1u);

    const auto created = sessions.CreateCharacter(connectionId, 0, "Cagri O'Kynareth");
    ASSERT_EQ(created.Status, CharacterCreateStatus::kSuccess);
    ASSERT_NE(created.CharacterId, 0u);
    EXPECT_EQ(sessions.Get(connectionId)->State, SessionState::kCharacterSelected);

    const auto record = repository.GetCharacterForOwner(static_cast<Persistence::CharacterId>(created.CharacterId), "new-player");
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->SlotIndex, 0);
    EXPECT_TRUE(record->NeedsRaceMenu);
    EXPECT_EQ(record->Race, GameId(0, 0x00013746));
    EXPECT_EQ(record->Cell, GameId(0, 0x000165A7));
    EXPECT_EQ(record->WorldSpace, GameId{});
    EXPECT_EQ(record->Level, 1);

    const auto snapshot = sessions.PrepareCharacterLoadSnapshot(connectionId);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->NeedsRaceMenu);
}

TEST_F(SessionServiceTest, EnforcesSlotLockOccupancyOwnerScopeAndNameUniqueness)
{
    sessions.SetCharacterSlotConfiguration(3, 2);
    const auto createFor = [this](const TiltedPhoques::ConnectionId_t aConnectionId, const char* acOwner, const std::uint32_t aSlot, const char* acName) {
        EXPECT_TRUE(sessions.Create(aConnectionId));
        EXPECT_TRUE(sessions.MarkAuthenticated(aConnectionId));
        EXPECT_TRUE(sessions.BindIdentity(aConnectionId, acOwner));
        return sessions.CreateCharacter(aConnectionId, aSlot, acName);
    };

    const auto created = createFor(119, "slot-owner", 0, "Alaric Stone");
    ASSERT_EQ(created.Status, CharacterCreateStatus::kSuccess);
    EXPECT_EQ(createFor(120, "slot-owner", 0, "Different Name").Status, CharacterCreateStatus::kSlotOccupied);
    EXPECT_EQ(createFor(121, "slot-owner", 1, "alaric stone").Status, CharacterCreateStatus::kNameTaken);
    EXPECT_EQ(createFor(122, "slot-owner", 2, "Locked Slot").Status, CharacterCreateStatus::kSlotLocked);
    EXPECT_EQ(createFor(123, "another-owner", 0, "Alaric Stone").Status, CharacterCreateStatus::kSuccess);
}

TEST_F(SessionServiceTest, StoresValidatedInitialCocPlacementForNewCharacter)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 124;
    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    ASSERT_TRUE(sessions.BindIdentity(connectionId, "spawn-owner"));
    const auto created = sessions.CreateCharacter(connectionId, 0, "Temple Spawn");
    ASSERT_EQ(created.Status, CharacterCreateStatus::kSuccess);
    ASSERT_TRUE(sessions.PrepareCharacterLoadSnapshot(connectionId).has_value());

    EXPECT_EQ(sessions.AcceptCharacterReady(connectionId, created.CharacterId, 1024.5f, -512.25f, 128.f), CharacterReadyStatus::kProceed);
    const auto record = repository.GetCharacterForOwner(static_cast<Persistence::CharacterId>(created.CharacterId), "spawn-owner");
    ASSERT_TRUE(record.has_value());
    EXPECT_FLOAT_EQ(record->PositionX, 1024.5f);
    EXPECT_FLOAT_EQ(record->PositionY, -512.25f);
    EXPECT_FLOAT_EQ(record->PositionZ, 128.f);
}

TEST_F(SessionServiceTest, SavesNewCharacterRaceAndSexAfterWorldEntry)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 125;
    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    ASSERT_TRUE(sessions.BindIdentity(connectionId, "appearance-owner"));
    const auto created = sessions.CreateCharacter(connectionId, 0, "Race Menu");
    ASSERT_EQ(created.Status, CharacterCreateStatus::kSuccess);
    ASSERT_TRUE(sessions.PrepareCharacterLoadSnapshot(connectionId).has_value());
    ASSERT_EQ(sessions.AcceptCharacterReady(connectionId, created.CharacterId, 100.f, 200.f, 300.f), CharacterReadyStatus::kProceed);
    ASSERT_TRUE(sessions.CompletePlayerAssignment(connectionId, static_cast<Persistence::CharacterId>(created.CharacterId)));

    const GameId selectedRace(0, 0x00013747);
    EXPECT_TRUE(sessions.UpdateSelectedCharacterAppearance(connectionId, selectedRace, 1));
    EXPECT_FALSE(sessions.UpdateSelectedCharacterAppearance(connectionId, selectedRace, 0));

    const auto record = repository.GetCharacterForOwner(static_cast<Persistence::CharacterId>(created.CharacterId), "appearance-owner");
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->Race, selectedRace);
    EXPECT_EQ(record->Sex, 1);
    EXPECT_FALSE(record->NeedsRaceMenu);
}

TEST_F(SessionServiceTest, RejectsEmptyAndRepeatedIdentityBindings)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 106;
    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));

    EXPECT_FALSE(sessions.BindIdentity(connectionId, ""));
    EXPECT_EQ(sessions.Get(connectionId)->State, SessionState::kAwaitingIdentity);
    EXPECT_FALSE(sessions.Get(connectionId)->OwnerProfileId.has_value());

    ASSERT_TRUE(sessions.BindIdentity(connectionId, "owner-profile"));
    EXPECT_FALSE(sessions.BindIdentity(connectionId, "replacement-profile"));
    EXPECT_EQ(sessions.Get(connectionId)->State, SessionState::kAwaitingCharacterSelection);
    ASSERT_TRUE(sessions.Get(connectionId)->OwnerProfileId.has_value());
    EXPECT_EQ(*sessions.Get(connectionId)->OwnerProfileId, "owner-profile");
}

TEST_F(SessionServiceTest, BindsDifferentOwnersForIndependentSessions)
{
    constexpr TiltedPhoques::ConnectionId_t firstConnectionId = 107;
    constexpr TiltedPhoques::ConnectionId_t secondConnectionId = 108;
    ASSERT_TRUE(sessions.Create(firstConnectionId));
    ASSERT_TRUE(sessions.Create(secondConnectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(firstConnectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(secondConnectionId));

    ASSERT_TRUE(sessions.BindIdentity(firstConnectionId, "first-owner"));
    ASSERT_TRUE(sessions.BindIdentity(secondConnectionId, "second-owner"));

    ASSERT_TRUE(sessions.Get(firstConnectionId)->OwnerProfileId.has_value());
    ASSERT_TRUE(sessions.Get(secondConnectionId)->OwnerProfileId.has_value());
    EXPECT_EQ(*sessions.Get(firstConnectionId)->OwnerProfileId, "first-owner");
    EXPECT_EQ(*sessions.Get(secondConnectionId)->OwnerProfileId, "second-owner");
    EXPECT_EQ(sessions.Get(firstConnectionId)->State, SessionState::kAwaitingCharacterSelection);
    EXPECT_EQ(sessions.Get(secondConnectionId)->State, SessionState::kAwaitingCharacterSelection);
}

TEST_F(SessionServiceTest, ListsOnlyBoundOwnersRecordsAndKeepsSqlLookingIdsSafe)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 102;
    const std::string owner = "owner-' OR 1=1 --";
    const auto firstId = repository.CreateCharacter(MakeCharacter(owner, "First", 12));
    (void)repository.CreateCharacter(MakeCharacter("other-owner", "Other", 99));

    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    ASSERT_TRUE(sessions.BindIdentity(connectionId, owner));

    const auto characters = sessions.ListCharacters(connectionId);
    ASSERT_TRUE(characters.has_value());
    ASSERT_EQ(characters->size(), 1u);
    EXPECT_EQ(characters->front().CharacterId, static_cast<std::uint64_t>(firstId));
    EXPECT_EQ(characters->front().Name, "First");
}

TEST_F(SessionServiceTest, UnboundCharacterListHasAnExplicitIdentityError)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 118;
    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));

    EXPECT_FALSE(sessions.ListCharacters(connectionId).has_value());
    EXPECT_EQ(sessions.GetCharacterListFailureStatus(connectionId), CharacterSelectionStatus::kIdentityNotReady);
}

TEST_F(SessionServiceTest, DevelopmentBootstrapSeedsEmptyListFromSaveAndKeepsExistingRecords)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 119;
    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    ASSERT_TRUE(sessions.BindIdentity(connectionId, "devtest:owner"));

    auto save = MakeCharacter("ignored-client-owner", "Saved Dragonborn", 42);
    save.Race = GameId(7, 0x00013746);
    save.WorldSpace = GameId(1, 0x0000003C);
    save.Cell = GameId(1, 0x0000003D);
    save.PositionX = 12.5f;
    save.PositionY = -23.25f;
    save.PositionZ = 300.f;
    save.Health = 100.f;
    save.Magicka = 80.f;
    save.Stamina = 90.f;

    EXPECT_EQ(sessions.CreateDevelopmentCharacterFromSaveIfEmpty(connectionId, save), DevelopmentCharacterBootstrapResult::kCreated);
    const auto characters = sessions.ListCharacters(connectionId);
    ASSERT_TRUE(characters.has_value());
    ASSERT_EQ(characters->size(), 1u);
    EXPECT_EQ(characters->front().Name, "Saved Dragonborn");
    EXPECT_EQ(characters->front().Race, save.Race);
    EXPECT_EQ(characters->front().Sex, save.Sex);
    EXPECT_EQ(characters->front().Level, 42);

    const auto character = repository.GetCharacterForOwner(static_cast<Persistence::CharacterId>(characters->front().CharacterId), "devtest:owner");
    ASSERT_TRUE(character.has_value());
    EXPECT_EQ(character->OwnerProfileId, "devtest:owner");
    EXPECT_EQ(character->WorldSpace, save.WorldSpace);
    EXPECT_EQ(character->Cell, save.Cell);
    EXPECT_FLOAT_EQ(character->PositionX, save.PositionX);
    EXPECT_FLOAT_EQ(character->PositionY, save.PositionY);
    EXPECT_FLOAT_EQ(character->PositionZ, save.PositionZ);
    EXPECT_EQ(sessions.CreateDevelopmentCharacterFromSaveIfEmpty(connectionId, save), DevelopmentCharacterBootstrapResult::kAlreadyExists);
    EXPECT_EQ(sessions.ListCharacters(connectionId)->size(), 1u);
}

TEST_F(SessionServiceTest, DevelopmentBootstrapRejectsUnboundOrInvalidSave)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 120;
    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    EXPECT_EQ(sessions.CreateDevelopmentCharacterFromSaveIfEmpty(connectionId, MakeCharacter("owner", "Save", 10)), DevelopmentCharacterBootstrapResult::kIdentityNotReady);

    ASSERT_TRUE(sessions.BindIdentity(connectionId, "devtest:owner"));
    auto invalidSave = MakeCharacter("owner", "Save", 10);
    invalidSave.Cell = {};
    EXPECT_EQ(sessions.CreateDevelopmentCharacterFromSaveIfEmpty(connectionId, invalidSave), DevelopmentCharacterBootstrapResult::kInvalidSave);
    EXPECT_TRUE(sessions.ListCharacters(connectionId)->empty());
}

TEST_F(SessionServiceTest, SelectsOwnedCharacterAndRejectsInvalidStateAfterSelection)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 103;
    const auto ownedId = repository.CreateCharacter(MakeCharacter("owner", "Owned", 10));
    const auto otherId = repository.CreateCharacter(MakeCharacter("other", "Other", 20));

    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    ASSERT_TRUE(sessions.BindIdentity(connectionId, "owner"));

    EXPECT_EQ(sessions.SelectCharacter(connectionId, static_cast<std::uint64_t>(ownedId)), CharacterSelectionStatus::kSuccess);
    ASSERT_TRUE(sessions.Get(connectionId)->SelectedCharacterId.has_value());
    EXPECT_EQ(*sessions.Get(connectionId)->SelectedCharacterId, ownedId);
    EXPECT_EQ(sessions.Get(connectionId)->State, SessionState::kCharacterSelected);

    EXPECT_EQ(sessions.SelectCharacter(connectionId, static_cast<std::uint64_t>(otherId)), CharacterSelectionStatus::kInvalidState);

    sessions.Remove(connectionId);
    EXPECT_EQ(sessions.Get(connectionId), nullptr);
}

TEST_F(SessionServiceTest, RejectsNotOwnedCharacterWithoutLeakingExistenceAndCleansUp)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 104;
    const auto otherId = repository.CreateCharacter(MakeCharacter("other", "Other", 20));

    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    ASSERT_TRUE(sessions.BindIdentity(connectionId, "owner"));

    EXPECT_EQ(sessions.SelectCharacter(connectionId, static_cast<std::uint64_t>(otherId)), CharacterSelectionStatus::kNotFoundOrNotOwned);
    EXPECT_EQ(sessions.SelectCharacter(connectionId, std::numeric_limits<std::uint64_t>::max()), CharacterSelectionStatus::kNotFoundOrNotOwned);
    EXPECT_EQ(sessions.Get(connectionId)->State, SessionState::kAwaitingCharacterSelection);

    sessions.Remove(connectionId);
    EXPECT_EQ(sessions.Get(connectionId), nullptr);
}

TEST_F(SessionServiceTest, RejectsSelectionBeforeIdentity)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 105;
    ASSERT_TRUE(sessions.Create(connectionId));
    EXPECT_EQ(sessions.SelectCharacter(connectionId, 1), CharacterSelectionStatus::kIdentityNotReady);

    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    ASSERT_TRUE(sessions.BindIdentity(connectionId, "owner"));
    EXPECT_EQ(sessions.SelectCharacter(connectionId, 1), CharacterSelectionStatus::kNotFoundOrNotOwned);
}

TEST_F(SessionServiceTest, RejectsSnapshotBeforeIdentityOrCharacterSelection)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 109;
    ASSERT_TRUE(sessions.Create(connectionId));
    EXPECT_FALSE(sessions.PrepareCharacterLoadSnapshot(connectionId).has_value());

    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    EXPECT_FALSE(sessions.PrepareCharacterLoadSnapshot(connectionId).has_value());

    ASSERT_TRUE(sessions.BindIdentity(connectionId, "owner"));
    EXPECT_FALSE(sessions.PrepareCharacterLoadSnapshot(connectionId).has_value());
    EXPECT_EQ(sessions.Get(connectionId)->State, SessionState::kAwaitingCharacterSelection);
}

TEST_F(SessionServiceTest, PreparesOwnerScopedSnapshotAndAwaitsClientReady)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 110;
    auto character = MakeCharacter("owner", "Persistent", 42);
    character.Race = GameId(0x01020304, 0x05060708);
    character.WorldSpace = GameId(0x11121314, 0x15161718);
    character.Cell = GameId(0x21222324, 0x25262728);
    character.PositionX = -123.5f;
    character.PositionY = 456.25f;
    character.PositionZ = 789.75f;
    character.Health = 321.5f;
    character.Magicka = 222.25f;
    character.Stamina = 111.75f;
    const auto characterId = repository.CreateCharacter(character);

    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    ASSERT_TRUE(sessions.BindIdentity(connectionId, "owner"));
    ASSERT_EQ(sessions.SelectCharacter(connectionId, static_cast<std::uint64_t>(characterId)), CharacterSelectionStatus::kSuccess);

    const auto snapshot = sessions.PrepareCharacterLoadSnapshot(connectionId);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->CharacterId, static_cast<std::uint64_t>(characterId));
    EXPECT_EQ(snapshot->Name, "Persistent");
    EXPECT_EQ(snapshot->Race, character.Race);
    EXPECT_EQ(snapshot->Sex, character.Sex);
    EXPECT_EQ(snapshot->Level, character.Level);
    EXPECT_EQ(snapshot->WorldSpaceId, character.WorldSpace);
    EXPECT_EQ(snapshot->CellId, character.Cell);
    EXPECT_EQ(snapshot->PositionX, character.PositionX);
    EXPECT_EQ(snapshot->PositionY, character.PositionY);
    EXPECT_EQ(snapshot->PositionZ, character.PositionZ);
    EXPECT_EQ(snapshot->Health, character.Health);
    EXPECT_EQ(snapshot->Magicka, character.Magicka);
    EXPECT_EQ(snapshot->Stamina, character.Stamina);
    EXPECT_EQ(sessions.Get(connectionId)->State, SessionState::kAwaitingClientReady);
    EXPECT_FALSE(sessions.CanProcessGameplay(connectionId));
}

TEST_F(SessionServiceTest, RejectsAnotherOwnersSelectedRecordAndResetsSafely)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 111;
    const auto otherCharacterId = repository.CreateCharacter(MakeCharacter("other-owner", "Other", 20));

    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    ASSERT_TRUE(sessions.BindIdentity(connectionId, "owner"));

    auto* session = sessions.Get(connectionId);
    ASSERT_NE(session, nullptr);
    session->SelectedCharacterId = otherCharacterId;
    session->State = SessionState::kCharacterSelected;

    EXPECT_FALSE(sessions.PrepareCharacterLoadSnapshot(connectionId).has_value());
    EXPECT_EQ(session->State, SessionState::kAwaitingCharacterSelection);
    EXPECT_FALSE(session->SelectedCharacterId.has_value());
}

TEST_F(SessionServiceTest, MissingSelectedRecordFailsSafely)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 112;
    const auto characterId = repository.CreateCharacter(MakeCharacter("owner", "Deleted", 20));

    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    ASSERT_TRUE(sessions.BindIdentity(connectionId, "owner"));
    ASSERT_EQ(sessions.SelectCharacter(connectionId, static_cast<std::uint64_t>(characterId)), CharacterSelectionStatus::kSuccess);
    ASSERT_TRUE(repository.DeleteCharacter(characterId, "owner"));

    EXPECT_FALSE(sessions.PrepareCharacterLoadSnapshot(connectionId).has_value());
    EXPECT_EQ(sessions.Get(connectionId)->State, SessionState::kAwaitingCharacterSelection);
    EXPECT_FALSE(sessions.Get(connectionId)->SelectedCharacterId.has_value());
}

TEST_F(SessionServiceTest, GameplayIsGatedUntilTheFutureInWorldState)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 113;
    ASSERT_TRUE(sessions.Create(connectionId));
    EXPECT_FALSE(sessions.CanProcessGameplay(connectionId));

    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    EXPECT_FALSE(sessions.CanProcessGameplay(connectionId));
    ASSERT_TRUE(sessions.BindIdentity(connectionId, "owner"));
    EXPECT_FALSE(sessions.CanProcessGameplay(connectionId));

    sessions.Get(connectionId)->State = SessionState::kAwaitingClientReady;
    EXPECT_FALSE(sessions.CanProcessGameplay(connectionId));
    sessions.Get(connectionId)->State = SessionState::kInWorld;
    EXPECT_TRUE(sessions.CanProcessGameplay(connectionId));
}

TEST_F(SessionServiceTest, ReadyRequestAdvancesOnlyToPlayerAssignment)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 114;
    auto character = MakeCharacter("owner", "Persistent", 42);
    character.WorldSpace = GameId(0, 0x00000001);
    character.Cell = GameId(0, 0x0000003C);
    character.PositionX = 10.f;
    character.PositionY = 20.f;
    character.PositionZ = 30.f;
    character.Health = 100.f;
    character.Magicka = 80.f;
    character.Stamina = 90.f;
    const auto characterId = repository.CreateCharacter(character);

    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    ASSERT_TRUE(sessions.BindIdentity(connectionId, "owner"));
    ASSERT_EQ(sessions.SelectCharacter(connectionId, static_cast<std::uint64_t>(characterId)), CharacterSelectionStatus::kSuccess);
    ASSERT_TRUE(sessions.PrepareCharacterLoadSnapshot(connectionId).has_value());

    EXPECT_EQ(sessions.AcceptCharacterReady(connectionId, static_cast<std::uint64_t>(characterId), 10.f, 20.f, 30.f), CharacterReadyStatus::kProceed);
    EXPECT_EQ(sessions.Get(connectionId)->State, SessionState::kAwaitingPlayerAssignment);
    EXPECT_TRUE(sessions.CanAssignPlayer(connectionId));
    EXPECT_FALSE(sessions.CanProcessGameplay(connectionId));

    const auto selected = sessions.GetSelectedCharacterForAssignment(connectionId);
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->Name, "Persistent");
    EXPECT_EQ(selected->Level, 42);
    EXPECT_EQ(selected->PositionX, 10.f);
    EXPECT_EQ(selected->Health, 100.f);

    ASSERT_TRUE(sessions.CompletePlayerAssignment(connectionId, characterId));
    EXPECT_EQ(sessions.Get(connectionId)->State, SessionState::kInWorld);
    EXPECT_TRUE(sessions.CanProcessGameplay(connectionId));
}

TEST_F(SessionServiceTest, ReadyRequestRejectsWrongCharacterIdWithoutEnteringWorld)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 115;
    auto character = MakeCharacter("owner", "Persistent", 42);
    character.Cell = GameId(0, 0x0000003C);
    const auto characterId = repository.CreateCharacter(character);

    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    ASSERT_TRUE(sessions.BindIdentity(connectionId, "owner"));
    ASSERT_EQ(sessions.SelectCharacter(connectionId, static_cast<std::uint64_t>(characterId)), CharacterSelectionStatus::kSuccess);
    ASSERT_TRUE(sessions.PrepareCharacterLoadSnapshot(connectionId).has_value());

    EXPECT_EQ(sessions.AcceptCharacterReady(connectionId, static_cast<std::uint64_t>(characterId + 1), 0.f, 0.f, 0.f), CharacterReadyStatus::kCharacterMismatchOrUnavailable);
    EXPECT_EQ(sessions.Get(connectionId)->State, SessionState::kAwaitingClientReady);
    EXPECT_FALSE(sessions.CanAssignPlayer(connectionId));
    EXPECT_FALSE(sessions.CanProcessGameplay(connectionId));
}

TEST_F(SessionServiceTest, ReadyRequestRejectsDeletedCharacterAndResetsSelection)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 116;
    auto character = MakeCharacter("owner", "Deleted", 20);
    character.Cell = GameId(0, 0x0000003C);
    const auto characterId = repository.CreateCharacter(character);

    ASSERT_TRUE(sessions.Create(connectionId));
    ASSERT_TRUE(sessions.MarkAuthenticated(connectionId));
    ASSERT_TRUE(sessions.BindIdentity(connectionId, "owner"));
    ASSERT_EQ(sessions.SelectCharacter(connectionId, static_cast<std::uint64_t>(characterId)), CharacterSelectionStatus::kSuccess);
    ASSERT_TRUE(sessions.PrepareCharacterLoadSnapshot(connectionId).has_value());
    ASSERT_TRUE(repository.DeleteCharacter(characterId, "owner"));

    EXPECT_EQ(sessions.AcceptCharacterReady(connectionId, static_cast<std::uint64_t>(characterId), 0.f, 0.f, 0.f), CharacterReadyStatus::kCharacterMismatchOrUnavailable);
    EXPECT_EQ(sessions.Get(connectionId)->State, SessionState::kAwaitingCharacterSelection);
    EXPECT_FALSE(sessions.Get(connectionId)->SelectedCharacterId.has_value());
    EXPECT_FALSE(sessions.CanProcessGameplay(connectionId));
}

TEST_F(SessionServiceTest, ReadyBeforeClientReadyIsRejected)
{
    constexpr TiltedPhoques::ConnectionId_t connectionId = 117;
    ASSERT_TRUE(sessions.Create(connectionId));
    EXPECT_EQ(sessions.AcceptCharacterReady(connectionId, 1, 0.f, 0.f, 0.f), CharacterReadyStatus::kInvalidState);
    EXPECT_FALSE(sessions.CanAssignPlayer(connectionId));
    EXPECT_FALSE(sessions.CanProcessGameplay(connectionId));
}
