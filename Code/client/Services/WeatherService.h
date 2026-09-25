#pragma once

struct World;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
struct AuthorityChangedEvent;
struct NotifyWeatherChange;

/**
 * @brief Replicates the server's canonical weather while one client reports engine-generated transitions.
 */
struct WeatherService
{
    WeatherService(World& aWorld, TransportService& aTransport, entt::dispatcher& aDispatcher);
    ~WeatherService() noexcept = default;

    TP_NOCOPYMOVE(WeatherService);

protected:
    void OnUpdate(const UpdateEvent& acEvent) noexcept;
    void OnDisconnected(const DisconnectedEvent& acEvent) noexcept;
    void OnAuthorityChangedEvent(const AuthorityChangedEvent& acEvent) noexcept;
    void OnWeatherChange(const NotifyWeatherChange& acMessage) noexcept;

    void RunWeatherUpdates(const double acDelta) noexcept;

    void ToggleGameWeatherSystem(bool aToggle) noexcept;
    void SetCachedWeather() noexcept;
    void SendCurrentWeatherProposal() noexcept;

private:
    World& m_world;
    TransportService& m_transport;

    /**
    * This variable has two uses:
    * For the elected reporter, it detects local weather transitions.
    * For other clients, it reapplies the server's canonical weather.
    */
    uint32_t m_cachedWeatherId{};
    uint32_t m_lastWorldAuthorityPlayerId{};
    bool m_hasWorldAuthoritySource{};
    bool m_waitingForServerWeather{};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectConnection;
    entt::scoped_connection m_authorityChangedConnection;
    entt::scoped_connection m_weatherChangeConnection;
};
