#include <Services/ContainerContentsCodec.h>

#include <catch2/catch.hpp>

namespace
{
Inventory::Entry Item(const int32_t aCount, const uint32_t aBaseId)
{
    Inventory::Entry entry{};
    entry.BaseId = GameId{0, aBaseId};
    entry.Count = aCount;
    return entry;
}
} // namespace

TEST_CASE("Container contents round-trip through the persisted hex form", "[container][persistence]")
{
    Inventory chest{};
    chest.Entries.push_back(Item(3, 0x13989));
    auto enchanted = Item(1, 0x12EB7);
    enchanted.ExtraEnchantId = GameId{0, 0x4605A};
    enchanted.ExtraEnchantCharge = 500;
    enchanted.ExtraHealth = 1.5f;
    chest.Entries.push_back(enchanted);

    const auto encoded = ContainerContentsCodec::Encode(chest);
    REQUIRE(encoded.has_value());
    REQUIRE_FALSE(encoded->empty());

    const auto decoded = ContainerContentsCodec::Decode(*encoded);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->Entries.size() == 2);
    CHECK(decoded->Entries[0].BaseId == GameId{0, 0x13989});
    CHECK(decoded->Entries[0].Count == 3);
    CHECK(decoded->Entries[1].ExtraEnchantId == GameId{0, 0x4605A});
    CHECK(decoded->Entries[1].ExtraEnchantCharge == 500);
    CHECK(decoded->Entries[1].ExtraHealth == 1.5f);
}

TEST_CASE("An emptied container persists as empty, not as missing", "[container][persistence]")
{
    // "Take all" leaves a real, empty chest; it must not fall back to the client baseline.
    const auto encoded = ContainerContentsCodec::Encode(Inventory{});
    REQUIRE(encoded.has_value());
    REQUIRE_FALSE(encoded->empty());

    const auto decoded = ContainerContentsCodec::Decode(*encoded);
    REQUIRE(decoded.has_value());
    CHECK(decoded->Entries.empty());
}

TEST_CASE("A large chest still fits the persisted form", "[container][persistence]")
{
    Inventory chest{};
    for (uint32_t i = 0; i < 500; ++i)
        chest.Entries.push_back(Item(static_cast<int32_t>(i + 1), 0x1000 + i));

    const auto encoded = ContainerContentsCodec::Encode(chest);
    REQUIRE(encoded.has_value());
    const auto decoded = ContainerContentsCodec::Decode(*encoded);
    REQUIRE(decoded.has_value());
    CHECK(decoded->Entries.size() == 500);
    CHECK(decoded->Entries[499].Count == 500);
}

TEST_CASE("Corrupt persisted container rows do not load", "[container][persistence]")
{
    Inventory chest{};
    chest.Entries.push_back(Item(3, 0x13989));
    const auto encoded = ContainerContentsCodec::Encode(chest);
    REQUIRE(encoded.has_value());

    CHECK_FALSE(ContainerContentsCodec::Decode("").has_value());
    CHECK_FALSE(ContainerContentsCodec::Decode("ABC").has_value());           // odd length
    CHECK_FALSE(ContainerContentsCodec::Decode("zz").has_value());            // not hex
    CHECK_FALSE(ContainerContentsCodec::Decode(encoded->substr(0, encoded->size() - 2)).has_value()); // truncated
    CHECK_FALSE(ContainerContentsCodec::Decode(*encoded + "00").has_value()); // trailing bytes

    Inventory negative{};
    negative.Entries.push_back(Item(-2, 0x13989));
    const auto encodedNegative = ContainerContentsCodec::Encode(negative);
    REQUIRE(encodedNegative.has_value());
    CHECK_FALSE(ContainerContentsCodec::Decode(*encodedNegative).has_value());
}
