#pragma once

#include <cstdint>

enum class PopulationAssignmentRejectionAction : std::uint8_t
{
    kSuppressLiveActor,
    kDestroyCancelledEntity,
};

struct PopulationSuppressionPolicy final
{
    [[nodiscard]] static constexpr PopulationAssignmentRejectionAction GetRejectionAction(const bool aAssignmentWasCancelled) noexcept
    {
        return aAssignmentWasCancelled ? PopulationAssignmentRejectionAction::kDestroyCancelledEntity : PopulationAssignmentRejectionAction::kSuppressLiveActor;
    }

    [[nodiscard]] static constexpr bool ShouldSkipAssignment(const bool aPopulationSuppressed) noexcept
    {
        return aPopulationSuppressed;
    }
};
