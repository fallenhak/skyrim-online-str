#include <Services/WeatherService.h>

#include <Components.h>
#include <GameServer.h>
#include <World.h>

#include <Messages/RequestWeatherChange.h>
#include <Messages/NotifyWeatherChange.h>
#include <Messages/RequestCurrentWeather.h>

WeatherService::WeatherService(World& aWorld, entt::dispatcher& aDispatcher) noexcept
    : m_world(aWorld)
{
    m_weatherChangeConnection = aDispatcher.sink<PacketEvent<RequestWeatherChange>>().connect<&WeatherService::OnWeatherChange>(this);
    m_currentWeatherConnection = aDispatcher.sink<PacketEvent<RequestCurrentWeather>>().connect<&WeatherService::OnRequestCurrentWeather>(this);
}

void WeatherService::OnWeatherChange(const PacketEvent<RequestWeatherChange>& acMessage) const noexcept
{
    NotifyWeatherChange notify{};
    notify.Id = acMessage.Packet.Id;

    if (!m_world.GetAuthorityService().TrySetWeatherState(acMessage.pPlayer, notify.Id))
        return;

    if (!acMessage.pPlayer->GetCharacter())
        return;

    const auto origin = *acMessage.pPlayer->GetCharacter();

    GameServer::Get()->SendToPartyInRange(notify, acMessage.pPlayer->GetParty(), origin, acMessage.pPlayer);
}

void WeatherService::OnRequestCurrentWeather(const PacketEvent<RequestCurrentWeather>& acMessage) const noexcept
{
    NotifyWeatherChange notify{};
    if (!m_world.GetAuthorityService().TryGetWeatherState(acMessage.pPlayer, notify.Id))
        return;

    acMessage.pPlayer->Send(notify);
}
