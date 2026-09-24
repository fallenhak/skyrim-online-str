#include <Structs/CharacterSessionOutboundPolicy.h>

bool CanSendCharacterProtocolMessage(const ClientOpcode aOpcode, const CharacterClientSessionPhase aPhase, const bool aIsLocalPlayerAssignment) noexcept
{
    switch (aOpcode)
    {
    case kAuthenticationRequest:
        return true;
    case kRequestCharacterList:
    case kSelectCharacterRequest:
    case kCreateCharacterRequest:
        return aPhase == CharacterClientSessionPhase::kAwaitingCharacterSelection;
    case kCharacterReadyRequest:
        return aPhase == CharacterClientSessionPhase::kAwaitingClientReady;
    case kAssignCharacterRequest:
        // Before world entry only the local player may be assigned; once in world, loaded
        // actors must reach the server or they stay client-local for every player.
        return (aPhase == CharacterClientSessionPhase::kAwaitingPlayerAssignment && aIsLocalPlayerAssignment) || aPhase == CharacterClientSessionPhase::kInWorld;
    default:
        return aPhase == CharacterClientSessionPhase::kInWorld;
    }
}
