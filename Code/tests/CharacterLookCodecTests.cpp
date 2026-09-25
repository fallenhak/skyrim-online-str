#include <Services/CharacterLookCodec.h>

#include <catch2/catch.hpp>

using namespace CharacterLookCodec;

namespace
{
Look MakeLook()
{
    Look look;
    look.ChangeFlags = 0x2000800;
    look.Appearance = std::string("head\0parts\xff", 11);
    look.Tints.push_back({6, 0xFF8844CC, 0.75f, "Actors/Character/Character Assets/TintMasks/SkinTone.dds"});
    look.Tints.push_back({1, 0x11223344, 0.f, ""});
    return look;
}
} // namespace

TEST_CASE("A stored look round-trips byte for byte", "[character_look]")
{
    const auto look = MakeLook();
    const auto encoded = Encode(look);
    REQUIRE(encoded.has_value());
    const auto decoded = Decode(*encoded);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == look);
}

TEST_CASE("A look without appearance data or over the limits is not stored", "[character_look]")
{
    auto look = MakeLook();
    look.Appearance.clear();
    CHECK_FALSE(Encode(look).has_value());

    look = MakeLook();
    look.Appearance.assign(kMaxAppearanceBytes + 1, 'x');
    CHECK_FALSE(Encode(look).has_value());

    look = MakeLook();
    look.Tints.resize(kMaxTints + 1);
    CHECK_FALSE(Encode(look).has_value());
}

TEST_CASE("Truncated, trailing or foreign blobs are rejected", "[character_look]")
{
    const auto encoded = *Encode(MakeLook());
    for (std::size_t size = 0; size < encoded.size(); ++size)
        CHECK_FALSE(Decode(std::string_view(encoded).substr(0, size)).has_value());

    CHECK_FALSE(Decode(encoded + "x").has_value());
    CHECK_FALSE(Decode("LK2" + encoded.substr(3)).has_value());
}

TEST_CASE("The stored text form survives binary bytes and rejects malformed hex", "[character_look]")
{
    const auto encoded = *Encode(MakeLook());
    const auto hex = ToHex(encoded);
    REQUIRE(hex.size() == encoded.size() * 2);
    const auto bytes = FromHex(hex);
    REQUIRE(bytes.has_value());
    CHECK(*bytes == encoded);
    CHECK(*Decode(*bytes) == MakeLook());

    CHECK_FALSE(FromHex("ABC").has_value());
    CHECK_FALSE(FromHex("zz").has_value());
    CHECK_FALSE(FromHex("ab").has_value());
}
