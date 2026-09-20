#pragma once

#include <Structs/CharacterAssignmentRejectReason.h>

#include <cstdint>

enum class PopulationAssignmentRejectionAction : std::uint8_t
{
    kSuppressLiveActor,
    kDestroyCancelledEntity,
};

struct PopulationSuppressionPolicy final
{
    static constexpr std::uint32_t kPlayerFormId = 0x14;

    [[nodiscard]] static constexpr PopulationAssignmentRejectionAction GetRejectionAction(const bool aAssignmentWasCancelled) noexcept
    {
        return aAssignmentWasCancelled ? PopulationAssignmentRejectionAction::kDestroyCancelledEntity : PopulationAssignmentRejectionAction::kSuppressLiveActor;
    }

    [[nodiscard]] static constexpr bool ShouldPhysicallySuppress(const CharacterAssignmentRejectReason aReason) noexcept
    {
        return aReason == CharacterAssignmentRejectReason::kPopulationHumanoidDenied;
    }

    [[nodiscard]] static constexpr bool ShouldPhysicallySuppress(const CharacterAssignmentRejectReason aReason, const bool aAssignmentWasCancelled) noexcept
    {
        return !aAssignmentWasCancelled && ShouldPhysicallySuppress(aReason);
    }

    [[nodiscard]] static constexpr bool ShouldOwnDisable(
        const CharacterAssignmentRejectReason aReason, const std::uint32_t aFormId, const bool aIsPlayer, const bool aIsTemporary, const bool aIsDeleted,
        const bool aWasAlreadyDisabled) noexcept
    {
        return ShouldPhysicallySuppress(aReason) && aFormId != kPlayerFormId && !aIsPlayer && !aIsTemporary && !aIsDeleted && !aWasAlreadyDisabled;
    }

    [[nodiscard]] static constexpr bool ShouldSkipAssignment(const bool aPopulationSuppressed) noexcept
    {
        return aPopulationSuppressed;
    }
};
