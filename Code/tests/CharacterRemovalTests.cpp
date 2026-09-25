// Message headers expect TiltedCore to be included first (the server gets it from its pch).
#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <Services/CharacterRemoval.h>

#include <catch2/catch.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
struct Journal
{
    std::vector<std::string> Events;
};

struct FakePlayer
{
    Journal* pJournal;
    std::string Name;
    std::vector<std::uint32_t> Removed;

    void Send(const ServerMessage& acMessage)
    {
        REQUIRE(acMessage.GetOpcode() == kNotifyRemoveCharacter);
        Removed.push_back(static_cast<const NotifyRemoveCharacter&>(acMessage).ServerId);
        pJournal->Events.push_back("send:" + Name);
    }
};
} // namespace

TEST_CASE("Character removal notifies every player, then destroys once", "[character_removal]")
{
    Journal journal;
    FakePlayer a{&journal, "a", {}};
    FakePlayer b{&journal, "b", {}};
    std::vector<FakePlayer*> players{&a, &b};
    int destroyed = 0;

    NotifyAndDestroyCharacter(
        0x1Au, players, [&] { journal.Events.push_back("script"); },
        [&] {
            ++destroyed;
            journal.Events.push_back("destroy");
        });

    REQUIRE(a.Removed == std::vector<std::uint32_t>{0x1Au});
    REQUIRE(b.Removed == std::vector<std::uint32_t>{0x1Au});
    REQUIRE(destroyed == 1);
    REQUIRE(journal.Events == std::vector<std::string>{"script", "send:a", "send:b", "destroy"});
}

TEST_CASE("Character removal with nobody connected still destroys the entity", "[character_removal]")
{
    std::vector<FakePlayer*> players;
    int destroyed = 0;
    bool scripted = false;

    NotifyAndDestroyCharacter(7u, players, [&] { scripted = true; }, [&] { ++destroyed; });

    REQUIRE(scripted);
    REQUIRE(destroyed == 1);
}

TEST_CASE("Disconnect cleanup never mistakes a character-less player for entity 0", "[character_removal][disconnect]")
{
    const std::optional<std::uint32_t> noCharacter;
    CHECK_FALSE(IsDisconnectingPlayersCharacter(noCharacter, std::uint32_t{0}));
    CHECK_FALSE(IsDisconnectingPlayersCharacter(noCharacter, std::uint32_t{7}));

    const std::optional<std::uint32_t> firstJoiner{0u};
    CHECK(IsDisconnectingPlayersCharacter(firstJoiner, std::uint32_t{0}));
    CHECK_FALSE(IsDisconnectingPlayersCharacter(firstJoiner, std::uint32_t{1}));
}
