#pragma once

#include <TiltedCore/Stl.hpp>

#include <cmath>
#include <cstdint>

// A player respawn is accepted by the server, so the server also decides the
// vitals the new incarnation starts with. Waiting for the owner to report its
// restored health left the canonical value below zero whenever that report
// never came (the client only sends values that differ from its own cache),
// and every autosave in the meantime was skipped as "character is dead".
struct PlayerRespawnVitalsPolicy final
{
    static constexpr uint32_t kHealthActorValue = 24;
    static constexpr uint32_t kMagickaActorValue = 25;
    static constexpr uint32_t kStaminaActorValue = 26;

    // Sets each vital that has a usable maximum to that maximum. Returns the
    // values that changed, ready to broadcast; vitals without a positive,
    // finite maximum are left untouched.
    [[nodiscard]] static TiltedPhoques::Map<uint32_t, float> RestoreToMax(
        TiltedPhoques::Map<uint32_t, float>& aCurrentValues, const TiltedPhoques::Map<uint32_t, float>& acMaxValues) noexcept
    {
        TiltedPhoques::Map<uint32_t, float> restored;
        for (const uint32_t actorValue : {kHealthActorValue, kMagickaActorValue, kStaminaActorValue})
        {
            const auto maxIt = acMaxValues.find(actorValue);
            if (maxIt == acMaxValues.end() || !std::isfinite(maxIt->second) || maxIt->second <= 0.f)
                continue;

            auto currentIt = aCurrentValues.find(actorValue);
            if (currentIt == aCurrentValues.end() || currentIt.value() == maxIt->second)
                continue;

            currentIt.value() = maxIt->second;
            restored.emplace(actorValue, maxIt->second);
        }
        return restored;
    }
};
