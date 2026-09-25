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
    m_weatherChangeConnection = aDispatcher.sink<NotifyWeatherChange>().connect<&WeatherService::OnWeatherChange>(this);
}

void WeatherService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    RunWeatherUpdates(acEvent.Delta);
}

void WeatherService::OnDisconnected(const DisconnectedEvent& acEvent) noexcept
{
    m_hasWorldAuthoritySource = false;
    m_lastWorldAuthorityPlayerId = 0;
    m_waitingForServerWeather = false;
    ToggleGameWeatherSystem(true);
}

void WeatherService::OnAuthorityChangedEvent(const AuthorityChangedEvent& acEvent) noexcept
{
    if (m_hasWorldAuthoritySource == acEvent.HasWorldAuthoritySource &&
        m_lastWorldAuthorityPlayerId == acEvent.WorldAuthorityPlayerId)
        return;

    m_hasWorldAuthoritySource = acEvent.HasWorldAuthoritySource;
    m_lastWorldAuthorityPlayerId = acEvent.WorldAuthorityPlayerId;

    if (!acEvent.HasWorldAuthoritySource)
    {
        spdlog::debug("[WeatherService] No world authority source; restoring local weather");
        ToggleGameWeatherSystem(true);
        return;
    }

    spdlog::info("[WeatherService] World weather reporter changed to player {}; requesting server state (local={})", acEvent.WorldAuthorityPlayerId,
        acEvent.HasLocalWorldAuthority);
    ToggleGameWeatherSystem(false);
}

void WeatherService::OnWeatherChange(const NotifyWeatherChange& acMessage) noexcept
{
    if (!acMessage.Id)
    {
        m_waitingForServerWeather = false;
        m_cachedWeatherId = 0;
        spdlog::debug("[WeatherService] Server has no canonical weather yet");

        if (m_world.GetAuthorityService().HasLocalWorldAuthority())
            SendCurrentWeatherProposal();
        return;
    }

    auto& modSystem = m_world.GetModSystem();
    const uint32_t weatherId = modSystem.GetGameId(acMessage.Id);
    TESWeather* pWeather = Cast<TESWeather>(TESForm::GetById(weatherId));

    if (!pWeather)
    {
        spdlog::error(__FUNCTION__ ": weather not found, form id: {:X}", acMessage.Id.ModId + acMessage.Id.BaseId);
        return;
    }

    m_waitingForServerWeather = false;
    Sky::Get()->ForceWeather(pWeather);

    m_cachedWeatherId = weatherId;
    spdlog::info("[WeatherService] Applied server weather mod {:X}, form {:X}", acMessage.Id.ModId, acMessage.Id.BaseId);
}

void WeatherService::RunWeatherUpdates(const double acDelta) noexcept
{
    if (m_waitingForServerWeather)
        return;

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
        SendCurrentWeatherProposal();
    else
    {
        SetCachedWeather();
    }
}

void WeatherService::ToggleGameWeatherSystem(bool aToggle) noexcept
{
    if (aToggle)
    {
        m_waitingForServerWeather = false;
        Sky::Get()->ReleaseWeatherOverride();
    }
    else
    {
        m_waitingForServerWeather = true;
        m_transport.Send(RequestCurrentWeather());
    }

    m_cachedWeatherId = 0;
}

void WeatherService::SendCurrentWeatherProposal() noexcept
{
    Sky* pSky = Sky::Get();
    TESWeather* pWeather = pSky ? pSky->GetWeather() : nullptr;
    if (!pWeather || pWeather->formID == 0xA6858 || pWeather->formID == m_cachedWeatherId)
        return;

    RequestWeatherChange request{};

    auto& modSystem = m_world.GetModSystem();
    if (!modSystem.GetServerModId(pWeather->formID, request.Id))
    {
        spdlog::error(__FUNCTION__ ": weather server ID not found, form id: {:X}", pWeather->formID);
        return;
    }

    m_cachedWeatherId = pWeather->formID;
    spdlog::debug("[WeatherService] Proposing local weather mod {:X}, form {:X} to the server", request.Id.ModId, request.Id.BaseId);
    m_transport.Send(request);
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
