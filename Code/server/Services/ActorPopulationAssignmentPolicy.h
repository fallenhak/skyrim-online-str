#pragma once

#include "ActorPopulationIdentityResolver.h"

#include <cstdint>

enum class ActorPopulationAssignmentDecision : std::uint8_t
{
    kAllow,
    kRejectHumanoid,
    kRejectUnknown,
};

class ActorPopulationAssignmentPolicy final
{
public:
    ActorPopulationAssignmentPolicy(bool aEnableHumanoidAssignmentGate = false, bool aAllowUnknownActorAssignments = true) noexcept
        : m_enableHumanoidAssignmentGate(aEnableHumanoidAssignmentGate)
        , m_allowUnknownActorAssignments(aAllowUnknownActorAssignments)
    {
    }

    [[nodiscard]] ActorPopulationAssignmentDecision Decide(const ActorPopulationIdentity& acIdentity) const noexcept;

private:
    bool m_enableHumanoidAssignmentGate{};
    bool m_allowUnknownActorAssignments{true};
};
