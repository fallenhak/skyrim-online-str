#include <Services/WeatherService.h>

#include <Events/UpdateEvent.h>
#include <Events/DisconnectedEvent.h>
#include <Events/AuthorityChangedEvent.h>

#include <Messages/RequestWeatherChange.h>
#include <Messages/NotifyWeatherChange.h>
#include <Messages/RequestCurrentWeather.h>

#include <Sky/Sky.h>
#include <Forms/TESWeather.h>

WeatherService::WeatherService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher)
    : m_world(aWorld)
    , m_transport(aTransport)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&WeatherService::OnUpdate>(this);
    m_disconnectConnection = aDispatcher.sink<DisconnectedEvent>().connect<&WeatherService::OnDisconnected>(this);
    m_authorityChangedConnection = aDispatcher.sink<AuthorityChangedEvent>().connect<&WeatherService::OnAuthorityChangedEvent>(this);
    m_playerAddedConnection = m_world.on_destroy<WaitingFor3D>().connect<&WeatherService::OnWaitingFor3DRemoved>(this);
    m_playerRemovedConnection = m_world.on_destroy<PlayerComponent>().connect<&WeatherService::OnPlayerComponentRemoved>(this);
    m_weatherChangeConnection = aDispatcher.sink<NotifyWeatherChange>().connect<&WeatherService::OnWeatherChange>(this);
}

void WeatherService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    RunWeatherUpdates(acEvent.Delta);
}

void WeatherService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    ToggleGameWeatherSystem(true);
}

void WeatherService::OnAuthorityChangedEvent(const AuthorityChangedEvent& acEvent) noexcept
{
    if (!acEvent.HasWorldAuthorityGroup)
    {
        ToggleGameWeatherSystem(true);
        return;
    }

    if (!acEvent.HasLocalWorldAuthority)
    {
        // Wait until the authority player's 3D is present before requesting its weather.
        auto view = m_world.view<PlayerComponent>();
        for (auto entity : view)
        {
            const auto& playerComponent = view.get<PlayerComponent>(entity);
            if (playerComponent.Id == acEvent.WorldAuthorityPlayerId)
            {
                ToggleGameWeatherSystem(false);
                break;
            }
        }

        return;
    }

    Sky* pSky = Sky::Get();
    if (!pSky)
        return;

    TESWeather* pWeather = pSky->GetWeather();
    if (!pWeather)
    {
        m_cachedWeatherId = 0;
        return;
    }

    // Potentially sets cached weather to map weather.
    // When the player closes the map, it'll send out the proper weather on the next update.
    m_cachedWeatherId = pWeather->formID;

    // This is the map weather, should not be synced.
    if (pWeather->formID == 0xA6858)
        return;

    RequestWeatherChange request{};

    auto& modSystem = m_world.GetModSystem();
    if (!modSystem.GetServerModId(pWeather->formID, request.Id))
    {
        spdlog::error(__FUNCTION__ ": weather server ID not found, form id: {:X}", pWeather->formID);
        return;
    }

    m_transport.Send(request);
}

// TODO: OnPlayerComponentAdded() instead? Does PlayerComponent exist already by then?
void WeatherService::OnWaitingFor3DRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    const auto* pPlayerComponent = m_world.try_get<PlayerComponent>(aEntity);
    if (!pPlayerComponent)
        return;

    const auto& authorityService = m_world.GetAuthorityService();
    if (!authorityService.HasWorldAuthorityGroup() || authorityService.HasLocalWorldAuthority())
        return;

    if (authorityService.GetWorldAuthorityPlayerId() == pPlayerComponent->Id)
        ToggleGameWeatherSystem(false);
}

void WeatherService::OnPlayerComponentRemoved(entt::registry& aRegistry, entt::entity aEntity) noexcept
{
    const auto& playerComponent = m_world.get<PlayerComponent>(aEntity);

    const auto& authorityService = m_world.GetAuthorityService();
    if (!authorityService.HasWorldAuthorityGroup() || authorityService.HasLocalWorldAuthority())
        return;

    if (authorityService.GetWorldAuthorityPlayerId() == playerComponent.Id)
        ToggleGameWeatherSystem(true);
}

void WeatherService::OnWeatherChange(const NotifyWeatherChange& acMessage) noexcept
{
    auto& modSystem = m_world.GetModSystem();
    const uint32_t weatherId = modSystem.GetGameId(acMessage.Id);
    TESWeather* pWeather = Cast<TESWeather>(TESForm::GetById(weatherId));

    if (!pWeather)
    {
        spdlog::error(__FUNCTION__ ": weather not found, form id: {:X}", acMessage.Id.ModId + acMessage.Id.BaseId);
        return;
    }

    Sky::Get()->ForceWeather(pWeather);

    m_cachedWeatherId = weatherId;
}

void WeatherService::RunWeatherUpdates(const double acDelta) noexcept
{
    Sky* pSky = Sky::Get();
    if (!pSky)
        return;

    TESWeather* pWeather = pSky->GetWeather();
    if (!pWeather)
    {
        if (m_world.GetAuthorityService().HasLocalWorldAuthority())
            m_cachedWeatherId = 0;
        else
            SetCachedWeather();

        return;
    }

    // This is the map weather, should not be synced.
    if (pWeather->formID == 0xA6858)
        return;

    // Have to manually check each frame because there's no singular SetWeather being used in-game.
    if (pWeather->formID == m_cachedWeatherId)
        return;

    if (m_world.GetAuthorityService().HasLocalWorldAuthority())
    {
        m_cachedWeatherId = pWeather->formID;

        RequestWeatherChange request{};

        auto& modSystem = m_world.GetModSystem();
        if (!modSystem.GetServerModId(pWeather->formID, request.Id))
        {
            spdlog::error(__FUNCTION__ ": weather server ID not found, form id: {:X}", pWeather->formID);
            return;
        }

        m_transport.Send(request);
    }
    else
    {
        SetCachedWeather();
    }
}

void WeatherService::ToggleGameWeatherSystem(bool aToggle) noexcept
{
    if (aToggle)
        Sky::Get()->ReleaseWeatherOverride();
    else
        m_transport.Send(RequestCurrentWeather());

    m_cachedWeatherId = 0;
}

void WeatherService::SetCachedWeather() noexcept
{
    if (m_cachedWeatherId == 0)
        return;

    TESWeather* pWeather = Cast<TESWeather>(TESForm::GetById(m_cachedWeatherId));

    if (!pWeather)
    {
        spdlog::error(__FUNCTION__ ": weather not found, form id: {:X}", m_cachedWeatherId);
        return;
    }

    Sky::Get()->ForceWeather(pWeather);
}
