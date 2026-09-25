#include <TiltedCore/Stl.hpp>

#include <Services/EncounterZoneIndex.h>

#include <catch2/catch.hpp>

namespace
{
constexpr uint32_t kBleakFallsZone = 0x0010A1;
constexpr uint32_t kBleakFallsLocation = 0x0200A1;
constexpr uint32_t kBleakFallsSanctum = 0x0200A2; // child location without its own zone
constexpr uint32_t kCellZone = 0x0010B1;
constexpr uint32_t kReferenceZone = 0x0010C1;

EncounterZoneIndex MakeIndex()
{
    EncounterZoneIndex index;
    index.AddZone(kBleakFallsZone, kBleakFallsLocation, {6, 0});
    index.AddZone(kCellZone, 0, {12, 24});
    index.AddZone(kReferenceZone, 0, {18, 0});
    index.AddLocationParent(kBleakFallsSanctum, kBleakFallsLocation);
    return index;
}
} // namespace

TEST_CASE("Encounter zone precedence is reference, cell, then location", "[deleveled_world]")
{
    const auto index = MakeIndex();
    using Source = EncounterZoneIndex::Source;

    auto result = index.Resolve({kReferenceZone, kCellZone, kBleakFallsLocation});
    REQUIRE(result.ZoneId == kReferenceZone);
    REQUIRE(result.From == Source::kReference);

    result = index.Resolve({0, kCellZone, kBleakFallsLocation});
    REQUIRE(result.ZoneId == kCellZone);
    REQUIRE(result.From == Source::kCell);

    result = index.Resolve({0, 0, kBleakFallsLocation});
    REQUIRE(result.ZoneId == kBleakFallsZone);
    REQUIRE(result.From == Source::kLocation);

    REQUIRE(index.FindRange(kCellZone) == EncounterZoneIndex::LevelRange{12, 24});
}

TEST_CASE("A child location inherits its parent's encounter zone", "[deleveled_world]")
{
    const auto index = MakeIndex();
    const auto result = index.Resolve({0, 0, kBleakFallsSanctum});
    REQUIRE(result.ZoneId == kBleakFallsZone);
    REQUIRE(result.From == EncounterZoneIndex::Source::kLocation);
}

TEST_CASE("Unknown zones and places without one resolve to no zone", "[deleveled_world]")
{
    const auto index = MakeIndex();
    // A zone id no plugin defines is ignored, so the next source is used.
    REQUIRE(index.Resolve({0x0DEAD1, 0, kBleakFallsLocation}).ZoneId == kBleakFallsZone);
    REQUIRE(index.Resolve({0, 0, 0x0300FF}).From == EncounterZoneIndex::Source::kNone);
    REQUIRE(index.Resolve({}).ZoneId == 0);
    REQUIRE_FALSE(index.FindRange(0x0DEAD1).has_value());
}

TEST_CASE("A cyclic location chain terminates", "[deleveled_world]")
{
    EncounterZoneIndex index;
    index.AddLocationParent(0x1, 0x2);
    index.AddLocationParent(0x2, 0x1);
    REQUIRE(index.Resolve({0, 0, 0x1}).From == EncounterZoneIndex::Source::kNone);
}
