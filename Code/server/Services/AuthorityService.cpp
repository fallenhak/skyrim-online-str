#include <Services/AuthorityService.h>

#include <Services/PartyService.h>
#include <World.h>

AuthorityService::AuthorityService(World& aWorld) noexcept
    : m_world(aWorld)
{
}

bool AuthorityService::CanClaimActor(Player* apClaimant, Player* apCurrentOwner) const noexcept
{
    if (!apClaimant || !apCurrentOwner)
        return false;

    auto& partyService = m_world.GetPartyService();
    if (!partyService.IsPlayerInParty(apClaimant) || !partyService.IsPlayerLeader(apClaimant))
        return false;

    PartyService::Party* const pParty = partyService.GetPlayerParty(apClaimant);
    if (!pParty)
        return false;

    return std::find(pParty->Members.begin(), pParty->Members.end(), apCurrentOwner) != pParty->Members.end();
}
