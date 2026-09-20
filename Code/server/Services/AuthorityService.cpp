#include <Services/AuthorityService.h>

#include <Services/PartyService.h>
#include <World.h>

AuthorityService::AuthorityService(World& aWorld) noexcept
    : m_world(aWorld)
{
}

bool AuthorityService::CanClaimActor(Player*, Player*) const noexcept
{
    // Persistent-world ownership is not transferred merely because another
    // player has a social/party role. Initial discovery assigns an owner and
    // CharacterService hands ownership to another in-range player when the
    // current owner relinquishes control or becomes unavailable.
    return false;
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
