#include <Structs/CharacterSessionOutboundPolicy.h>

bool CanSendCharacterProtocolMessage(const ClientOpcode aOpcode, const CharacterClientSessionPhase aPhase, const bool aIsLocalPlayerAssignment) noexcept
{
    switch (aOpcode)
    {
    case kAuthenticationRequest:
        return true;
    case kRequestCharacterList:
    case kSelectCharacterRequest:
        return aPhase == CharacterClientSessionPhase::kAwaitingCharacterSelection;
    case kCharacterReadyRequest:
        return aPhase == CharacterClientSessionPhase::kAwaitingClientReady;
    case kAssignCharacterRequest:
        return aPhase == CharacterClientSessionPhase::kAwaitingPlayerAssignment && aIsLocalPlayerAssignment;
    default:
        return aPhase == CharacterClientSessionPhase::kInWorld;
    }
}
