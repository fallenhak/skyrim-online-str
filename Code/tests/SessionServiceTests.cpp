#include <Services/SessionService.h>

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
