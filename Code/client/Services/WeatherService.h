#pragma once

struct World;
struct TransportService;
struct UpdateEvent;
struct DisconnectedEvent;
struct AuthorityChangedEvent;
struct NotifyWeatherChange;

/**
 * @brief Responsible for weather changes, which is controlled by the shared world's elected authority client.
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

private:
    World& m_world;
    TransportService& m_transport;

    /**
    * This variable has two uses:
    * For the party leader, it is used to detect weather changes.
    * For non-leaders, it is used to reapply the server weather if it changes.
    */
    uint32_t m_cachedWeatherId{};

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_disconnectConnection;
    entt::scoped_connection m_authorityChangedConnection;
    entt::scoped_connection m_weatherChangeConnection;
};
