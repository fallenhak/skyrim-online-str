#include "ActorPopulationAssignmentPolicy.h"

ActorPopulationAssignmentDecision ActorPopulationAssignmentPolicy::Decide(const ActorPopulationIdentity& acIdentity) const noexcept
{
    if (acIdentity.IsPlayer || !m_enableHumanoidAssignmentGate)
        return ActorPopulationAssignmentDecision::kAllow;

    if (acIdentity.IsTrusted() && acIdentity.Classification.Class == ActorPopulationClass::kHumanoidNpc)
        return ActorPopulationAssignmentDecision::kRejectHumanoid;

    if (acIdentity.IsTrusted() && acIdentity.Classification.Class == ActorPopulationClass::kCreature)
        return ActorPopulationAssignmentDecision::kAllow;

    return m_allowUnknownActorAssignments ? ActorPopulationAssignmentDecision::kAllow : ActorPopulationAssignmentDecision::kRejectUnknown;
}
