#include <Services/PlayerRespawnVitalsPolicy.h>

#include <catch2/catch.hpp>

#include <limits>

namespace
{
constexpr uint32_t kHealth = PlayerRespawnVitalsPolicy::kHealthActorValue;
constexpr uint32_t kMagicka = PlayerRespawnVitalsPolicy::kMagickaActorValue;
constexpr uint32_t kStamina = PlayerRespawnVitalsPolicy::kStaminaActorValue;
} // namespace

TEST_CASE("Respawn restores a dead player's vitals to their maximum", "[player_respawn]")
{
    TiltedPhoques::Map<uint32_t, float> current{{kHealth, -37.f}, {kMagicka, 12.f}, {kStamina, 100.f}};
    const TiltedPhoques::Map<uint32_t, float> max{{kHealth, 150.f}, {kMagicka, 110.f}, {kStamina, 100.f}};

    const auto restored = PlayerRespawnVitalsPolicy::RestoreToMax(current, max);

    REQUIRE(current.at(kHealth) == 150.f);
    REQUIRE(current.at(kMagicka) == 110.f);
    REQUIRE(current.at(kStamina) == 100.f);
    // Stamina was already full, so only the changed values are broadcast.
    REQUIRE(restored.size() == 2);
    REQUIRE(restored.at(kHealth) == 150.f);
    REQUIRE(restored.at(kMagicka) == 110.f);
}

TEST_CASE("Respawn leaves vitals without a usable maximum untouched", "[player_respawn]")
{
    TiltedPhoques::Map<uint32_t, float> current{{kHealth, -5.f}, {kMagicka, 3.f}};
    const TiltedPhoques::Map<uint32_t, float> max{
        {kHealth, 0.f}, {kMagicka, std::numeric_limits<float>::quiet_NaN()}, {kStamina, 100.f}};

    const auto restored = PlayerRespawnVitalsPolicy::RestoreToMax(current, max);

    REQUIRE(restored.empty());
    REQUIRE(current.at(kHealth) == -5.f);
    REQUIRE(current.at(kMagicka) == 3.f);
    // A vital the server never received is not invented.
    REQUIRE(current.find(kStamina) == current.end());
}
