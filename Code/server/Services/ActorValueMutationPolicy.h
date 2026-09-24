#pragma once

#include <cmath>
#include <cstdint>

struct ActorValueMutationPolicy final
{
    // Mirrors Skyrim's ActorValueInfo::kActorValueCount without coupling the
    // dedicated server target to the client-only Forms headers.
    static constexpr std::uint32_t kActorValueCount = 164;

    [[nodiscard]] static bool IsValidIndexAndValue(const std::uint32_t aActorValue, const float aValue,
                                                   const std::uint32_t aActorValueCount) noexcept
    {
        return aActorValue < aActorValueCount && std::isfinite(aValue);
    }
};
