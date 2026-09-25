#pragma once

#include <Events/PacketEvent.h>
#include <DateTime.h>
#include <Structs/GameId.h>

struct World;
struct UpdateEvent;
struct PlayerJoinEvent;

namespace Persistence
{
struct WorldClockRepository;
}

/**
 * @brief Manages time and date of the world.
 */
class CalendarService
{
public:
    CalendarService(World& aWorld, entt::dispatcher& aDispatcher, Persistence::WorldClockRepository& aRepository);

    // we use these types for SOL
    // this is done this way because SOL
    // provides direct support for these
    using TTime = std::pair<int, int>;
    using TDate = std::tuple<int, int, int>;

    bool SetTime(int aHour, int aMinutes, float aScale) noexcept;
    bool SetDate(int aDay, int aMonth, float aYear) noexcept;

    // returns hours, minutes
    TTime GetTime() const noexcept;
    static TTime GetRealTime() noexcept;

    // returns dd/mm/yy
    TDate GetDate() const noexcept;

    float GetTimeScale() const noexcept { return m_dateTime.m_timeModel.TimeScale; }
    bool SetTimeScale(float aScale) noexcept;

    // Continues the clock saved before the last shutdown. False when there is none.
    bool RestoreSavedClock() noexcept;
    void SaveClock() noexcept;

private:
    void OnUpdate(const UpdateEvent&) noexcept;
    void OnPlayerJoin(const PlayerJoinEvent&) noexcept;
    void SendTimeResync() noexcept;

    DateTime m_dateTime;
    uint64_t m_lastTick = 0;
    float m_secondsSinceSave = 0.f;
    bool m_timeInitialized = false;

    World& m_world;
    Persistence::WorldClockRepository& m_repository;

    entt::scoped_connection m_updateConnection;
    entt::scoped_connection m_joinConnection;
};
