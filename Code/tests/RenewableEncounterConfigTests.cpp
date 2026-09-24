#include <Services/RenewableEncounterConfig.h>

#include <catch2/catch.hpp>

#include <sstream>

namespace
{
RenewableEncounterConfigResult Load(const std::string& acText, RenewableEncounterRegistry& aRegistry)
{
    std::istringstream stream(acText);
    return LoadRenewableEncounterConfig(stream, aRegistry);
}
} // namespace

TEST_CASE("W12: a well-formed config builds encounters, cell sets and slots", "[renewable_encounter]")
{
    RenewableEncounterRegistry registry;
    const auto result = Load(
        "# Bleak Falls Barrow\n"
        "encounter 0001A2B3 0 cooldown=60\n"
        "cell      0001A2B3 0 0001A2B4\n"
        "slot      0001A2B3 0 0010F00D\n"
        "slot      0001A2B3 0 0010F00E\n"
        "\n"
        "encounter 0x0002C3D4 1\n"
        "slot 0002C3D4 1 0010F10A   # trailing comment\n",
        registry);

    REQUIRE(result.Errors.empty());
    REQUIRE(result.Encounters == 2);
    REQUIRE(result.Cells == 1);
    REQUIRE(result.Slots == 3);

    const RenewableEncounterId barrow{0x0001A2B3u, 0};
    REQUIRE(registry.Find(barrow));
    REQUIRE(registry.FindEncounter(SpawnSlotId{0x0010F00Eu}) == barrow);
    REQUIRE(registry.FindEncounter(SpawnSlotId{0x0010F10Au}) == RenewableEncounterId{0x0002C3D4u, 1});

    // The extra cell is part of the occupancy scope.
    REQUIRE(registry.SetPlayerCell(7, 0x0001A2B4u));
    REQUIRE(registry.IsOccupied(barrow));
}

TEST_CASE("W12: cooldown is in seconds and defaults to the policy default", "[renewable_encounter]")
{
    RenewableEncounterRegistry registry;
    const auto result = Load("encounter 00000100 0 cooldown=60\nslot 00000100 0 00000A01\n"
                             "encounter 00000200 0\nslot 00000200 0 00000A02\n",
                             registry);
    REQUIRE(result.Errors.empty());

    REQUIRE(registry.BindIncarnation({0x100, 0}, SpawnSlotId{0xA01}, {1, 1}, 0));
    REQUIRE(registry.RecordVerifiedDeath({1, 1}, 10) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(registry.Find({0x100, 0})->GetResetCooldownRemaining(10) == 60);

    REQUIRE(registry.BindIncarnation({0x200, 0}, SpawnSlotId{0xA02}, {2, 1}, 0));
    REQUIRE(registry.RecordVerifiedDeath({2, 1}, 10) == RenewableEncounterState::DeathResult::Recorded);
    REQUIRE(registry.Find({0x200, 0})->GetResetCooldownRemaining(10) == RenewableEncounterPolicy{}.ResetCooldownTicks);
}

TEST_CASE("W12: bad lines are reported with their line number and skipped", "[renewable_encounter]")
{
    RenewableEncounterRegistry registry;
    const auto result = Load(
        "encounter 00000100 0\n"       // 1 ok
        "slot 00000999 0 00000A01\n"   // 2 unknown encounter
        "encounter 00000100 0\n"       // 3 duplicate
        "slot 00000100 0 00000A01\n"   // 4 ok
        "slot 00000100 0 00000A01\n"   // 5 slot already used
        "encounter 0 0\n"              // 6 invalid cell
        "encounter 00000300 x\n"       // 7 bad group
        "encounter 00000400 0 cooldown=abc\n" // 8 bad option
        "spawn 00000100 0 00000A02\n"  // 9 unknown directive
        "cell 00000100 0\n"            // 10 missing field
        "slot 00000100 0 00000A03 extra\n", // 11 trailing field
        registry);

    REQUIRE(result.Encounters == 1);
    REQUIRE(result.Slots == 1);
    REQUIRE(result.Errors.size() == 9);
    REQUIRE(result.Errors[0].rfind("line 2:", 0) == 0);
    REQUIRE(result.Errors[8].rfind("line 11:", 0) == 0);
    REQUIRE(registry.GetEncounterCount() == 1);
}

TEST_CASE("W12: an empty config is valid and adds nothing", "[renewable_encounter]")
{
    RenewableEncounterRegistry registry;
    const auto result = Load("# nothing configured yet\n\n", registry);
    REQUIRE(result.Errors.empty());
    REQUIRE(result.Encounters == 0);
    REQUIRE(registry.GetEncounterCount() == 0);
}
