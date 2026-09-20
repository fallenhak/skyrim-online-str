#include <TiltedCore/Stl.hpp>
#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Buffer.hpp>
#include <TiltedCore/Serialization.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <catch2/catch.hpp>

#include <Messages/NotifyProgressionAward.h>
#include <Messages/ServerMessageFactory.h>
#include <Structs/ProgressionAwardPolicy.h>

#include <cmath>
#include <limits>
#include <utility>

TEST_CASE("Progression skills have stable protocol values", "[progression]")
{
    REQUIRE(static_cast<unsigned>(ProgressionSkill::kOneHanded) == 0u);
    REQUIRE(static_cast<unsigned>(ProgressionSkill::kArchery) == 2u);
    REQUIRE(static_cast<unsigned>(ProgressionSkill::kSpeech) == 11u);
    REQUIRE(static_cast<unsigned>(ProgressionSkill::kEnchanting) == 17u);
    REQUIRE(static_cast<unsigned>(ProgressionSkill::kCount) == 18u);

    REQUIRE(IsValidProgressionSkill(ProgressionSkill::kRestoration));
    REQUIRE_FALSE(IsValidProgressionSkill(static_cast<ProgressionSkill>(0xff)));
    REQUIRE(IsValidProgressionAwardReason(ProgressionAwardReason::kScripted));
    REQUIRE_FALSE(IsValidProgressionAwardReason(static_cast<ProgressionAwardReason>(0xff)));
}

TEST_CASE("Progression award round trips wide ids", "[progression][encoding]")
{
    NotifyProgressionAward sent{};
    sent.AwardId = std::numeric_limits<std::uint64_t>::max();
    sent.CharacterId = std::numeric_limits<std::uint64_t>::max() - 1;
    sent.Skill = ProgressionSkill::kEnchanting;
    sent.Experience = 123.5f;
    sent.Reason = ProgressionAwardReason::kScripted;

    TiltedPhoques::Buffer buffer(256);
    TiltedPhoques::Buffer::Writer writer(&buffer);
    sent.Serialize(writer);

    TiltedPhoques::Buffer::Reader reader(&buffer);
    const ServerMessageFactory factory;
    auto message = factory.Extract(reader);
    REQUIRE(message);

    auto received = TiltedPhoques::CastUnique<NotifyProgressionAward>(std::move(message));
    REQUIRE(received);
    REQUIRE(*received == sent);
}

TEST_CASE("Progression award policy rejects unsafe values", "[progression][policy]")
{
    const auto valid = [](const std::uint64_t aAwardId, const std::uint64_t aCharacterId, const float aExperience, const bool aInWorld = true)
    {
        return IsValidProgressionAward(
            aAwardId,
            aCharacterId,
            ProgressionSkill::kOneHanded,
            ProgressionAwardReason::kCreatureKill,
            aExperience,
            aInWorld,
            aCharacterId);
    };

    REQUIRE(valid(1, 42, 1.0f));
    REQUIRE_FALSE(valid(0, 42, 1.0f));
    REQUIRE_FALSE(valid(1, 0, 1.0f));
    REQUIRE_FALSE(valid(1, 42, 1.0f, false));
    REQUIRE_FALSE(IsValidProgressionAward(1, 7, ProgressionSkill::kOneHanded, ProgressionAwardReason::kCreatureKill, 1.0f, true, 42));
    REQUIRE_FALSE(IsValidProgressionAward(1, 42, static_cast<ProgressionSkill>(0xff), ProgressionAwardReason::kCreatureKill, 1.0f, true, 42));
    REQUIRE_FALSE(IsValidProgressionAward(1, 42, ProgressionSkill::kOneHanded, static_cast<ProgressionAwardReason>(0xff), 1.0f, true, 42));
    REQUIRE_FALSE(valid(1, 42, 0.0f));
    REQUIRE_FALSE(valid(1, 42, -1.0f));
    REQUIRE_FALSE(valid(1, 42, std::numeric_limits<float>::quiet_NaN()));
    REQUIRE_FALSE(valid(1, 42, std::numeric_limits<float>::infinity()));
    REQUIRE_FALSE(valid(1, 42, kMaxProgressionAwardExperience + 1.0f));
}

TEST_CASE("Progression award ids are deduplicated in a bounded window", "[progression][policy]")
{
    ProgressionAwardDeduplication<3> deduplication;

    REQUIRE(deduplication.TryRemember(10));
    REQUIRE_FALSE(deduplication.TryRemember(10));
    REQUIRE(deduplication.TryRemember(11));
    REQUIRE(deduplication.TryRemember(12));
    REQUIRE_FALSE(deduplication.TryRemember(11));
    REQUIRE(deduplication.TryRemember(13));
    REQUIRE(deduplication.TryRemember(10));
    REQUIRE(deduplication.TryRemember(11));
    REQUIRE_FALSE(deduplication.TryRemember(0));
}

TEST_CASE("Progression authority policy keeps offline play vanilla and persistent level server-authoritative", "[progression][policy]")
{
    REQUIRE_FALSE(ShouldUseServerControlledProgression(false));
    REQUIRE(ShouldUseServerControlledProgression(true));
    REQUIRE(ShouldAcceptClientLevel(false));
    REQUIRE_FALSE(ShouldAcceptClientLevel(true));
}
