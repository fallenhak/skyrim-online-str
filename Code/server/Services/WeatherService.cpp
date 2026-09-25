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

void WeatherService::OnWeatherChange(const PacketEvent<RequestWeatherChange>& acMessage) noexcept
{
    NotifyWeatherChange notify{};
    notify.Id = acMessage.Packet.Id;

    if (!m_world.GetAuthorityService().IsWorldAuthority(acMessage.pPlayer))
    {
        spdlog::debug("[WeatherService] Ignored weather proposal from non-authority player {}", acMessage.pPlayer->GetId());
        return;
    }

    if (!m_weatherState.SetCurrent(notify.Id))
        return;

    spdlog::info("[WeatherService] Server weather changed from player {} to mod {:X}, form {:X}", acMessage.pPlayer->GetId(), notify.Id.ModId, notify.Id.BaseId);

    GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
}

void WeatherService::OnRequestCurrentWeather(const PacketEvent<RequestCurrentWeather>& acMessage) const noexcept
{
    NotifyWeatherChange notify{};
    const bool hasWeather = m_weatherState.TryGetCurrent(notify.Id);
    spdlog::info("[WeatherService] Sent canonical weather to player {} (set={}, mod={:X}, form={:X})", acMessage.pPlayer->GetId(), hasWeather,
        notify.Id.ModId, notify.Id.BaseId);

    // An empty ID tells a newly elected reporter that the server has not received
    // an initial weather proposal yet.
    acMessage.pPlayer->Send(notify);
}
