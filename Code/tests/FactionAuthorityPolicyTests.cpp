#include <Structs/FactionAuthorityPolicy.h>

#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <catch2/catch.hpp>

#include <limits>

TEST_CASE("Faction updates require the current owner incarnation", "[actor_authority]")
{
    REQUIRE(FactionAuthorityPolicy::IsAuthorized(true, true, true, false, 8, 8));
    REQUIRE_FALSE(FactionAuthorityPolicy::IsAuthorized(true, true, false, false, 8, 8));
    REQUIRE_FALSE(FactionAuthorityPolicy::IsAuthorized(true, true, true, false, 9, 8));
    REQUIRE_FALSE(FactionAuthorityPolicy::IsAuthorized(true, true, true, false, 8, 0));
    REQUIRE_FALSE(FactionAuthorityPolicy::IsAuthorized(false, true, true, false, 8, 8));
    REQUIRE_FALSE(FactionAuthorityPolicy::IsAuthorized(true, false, true, false, 8, 8));
}

TEST_CASE("Persistent player factions remain owner-only", "[actor_authority]")
{
    REQUIRE(FactionAuthorityPolicy::IsAuthorized(true, true, true, true, 8, 8));
    REQUIRE_FALSE(FactionAuthorityPolicy::IsAuthorized(true, true, false, true, 8, 8));
}

TEST_CASE("Faction payloads reject invalid and duplicate entries", "[actor_authority]")
{
    Factions factions;
    REQUIRE(FactionAuthorityPolicy::HasValidPayload(factions));

    Faction validFaction;
    validFaction.Id = GameId(0, 0x123);
    validFaction.Rank = 1;
    factions.NpcFactions.push_back(validFaction);
    REQUIRE(FactionAuthorityPolicy::HasValidPayload(factions));

    factions.ExtraFactions.push_back(validFaction);
    REQUIRE(FactionAuthorityPolicy::HasValidPayload(factions));

    factions.NpcFactions.push_back(validFaction);
    REQUIRE_FALSE(FactionAuthorityPolicy::HasValidPayload(factions));

    factions.NpcFactions.pop_back();
    factions.ExtraFactions.clear();
    factions.NpcFactions[0].Id = GameId{};
    REQUIRE_FALSE(FactionAuthorityPolicy::HasValidPayload(factions));

    factions.NpcFactions[0] = validFaction;
    factions.NpcFactions.resize(FactionAuthorityPolicy::kMaxFactionEntriesPerList + 1, validFaction);
    REQUIRE_FALSE(FactionAuthorityPolicy::HasValidPayload(factions));
}

TEST_CASE("Faction update encoding carries the epoch and rejects oversized lists", "[actor_authority]")
{
    FactionUpdate sent;
    sent.OwnershipEpoch = 7;
    Faction faction;
    faction.Id = GameId(2, 0x345);
    faction.Rank = -1;
    sent.FactionsContent.NpcFactions.push_back(faction);

    TiltedPhoques::Buffer buffer(128);
    {
        TiltedPhoques::Buffer::Writer writer(&buffer);
        sent.Serialize(writer);
    }

    FactionUpdate received;
    {
        TiltedPhoques::Buffer::Reader reader(&buffer);
        REQUIRE(received.Deserialize(reader));
    }
    REQUIRE(received == sent);

    TiltedPhoques::Buffer malformedBuffer(32);
    {
        TiltedPhoques::Buffer::Writer writer(&malformedBuffer);
        TiltedPhoques::Serialization::WriteVarInt(writer, 7);
        TiltedPhoques::Serialization::WriteVarInt(writer, FactionAuthorityPolicy::kMaxFactionEntriesPerList + 1);
    }

    FactionUpdate malformed;
    TiltedPhoques::Buffer::Reader malformedReader(&malformedBuffer);
    REQUIRE_FALSE(malformed.Deserialize(malformedReader));

    TiltedPhoques::Buffer malformedExtraBuffer(32);
    {
        TiltedPhoques::Buffer::Writer writer(&malformedExtraBuffer);
        TiltedPhoques::Serialization::WriteVarInt(writer, 7);
        TiltedPhoques::Serialization::WriteVarInt(writer, 0);
        TiltedPhoques::Serialization::WriteVarInt(writer, FactionAuthorityPolicy::kMaxFactionEntriesPerList + 1);
    }

    TiltedPhoques::Buffer::Reader malformedExtraReader(&malformedExtraBuffer);
    REQUIRE_FALSE(malformed.Deserialize(malformedExtraReader));

    TiltedPhoques::Buffer malformedIdBuffer(32);
    {
        TiltedPhoques::Buffer::Writer writer(&malformedIdBuffer);
        TiltedPhoques::Serialization::WriteVarInt(writer, 7);
        TiltedPhoques::Serialization::WriteVarInt(writer, 1);
        TiltedPhoques::Serialization::WriteVarInt(writer, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);
        TiltedPhoques::Serialization::WriteVarInt(writer, 0);
    }

    TiltedPhoques::Buffer::Reader malformedIdReader(&malformedIdBuffer);
    REQUIRE_FALSE(malformed.Deserialize(malformedIdReader));

    TiltedPhoques::Buffer oversizedEpochBuffer(32);
    {
        TiltedPhoques::Buffer::Writer writer(&oversizedEpochBuffer);
        TiltedPhoques::Serialization::WriteVarInt(writer, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);
    }

    TiltedPhoques::Buffer::Reader oversizedEpochReader(&oversizedEpochBuffer);
    REQUIRE_FALSE(malformed.Deserialize(oversizedEpochReader));
}
