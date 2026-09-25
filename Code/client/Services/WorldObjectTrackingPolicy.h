#pragma once

namespace WorldObjectTrackingPolicy
{
[[nodiscard]] constexpr bool ShouldTrackWorldItem(const bool aIsConnected, const bool aPickupSucceeded) noexcept
{
    return aIsConnected && aPickupSucceeded;
}

[[nodiscard]] constexpr bool ShouldTrackHarvest(const bool aIsConnected, const bool aWouldTrackHarvest) noexcept
{
    return aIsConnected && aWouldTrackHarvest;
}
} // namespace WorldObjectTrackingPolicy
