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

bool AuthorityService::TrySetWeatherState(Player* apPlayer, const GameId& acWeather) const noexcept
{
    if (!apPlayer)
        return false;

    PartyService::Party* const pParty = m_world.GetPartyService().GetPlayerParty(apPlayer);
    if (!pParty)
        return false;

    pParty->CachedWeather = acWeather;
    return true;
}

bool AuthorityService::TryGetWeatherState(Player* apPlayer, GameId& aWeather) const noexcept
{
    if (!apPlayer)
        return false;

    PartyService::Party* const pParty = m_world.GetPartyService().GetPlayerParty(apPlayer);
    if (!pParty)
        return false;

    aWeather = pParty->CachedWeather;
    return true;
}
