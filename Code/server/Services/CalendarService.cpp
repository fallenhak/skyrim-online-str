#include <Services/CalendarService.h>

#include <GameServer.h>
#include <World.h>

#include <Events/PlayerJoinEvent.h>
#include <Events/UpdateEvent.h>

#include <Messages/ServerTimeSettings.h>

#include "Game/Player.h"

#include <Persistence/WorldClockRepository.h>

#include <exception>
#include <optional>

namespace
{
// Real seconds between clock saves; a crash loses at most this much (x timescale) of game time.
constexpr float kClockSaveIntervalSeconds = 30.f;
} // namespace

CalendarService::CalendarService(World& aWorld, entt::dispatcher& aDispatcher, Persistence::WorldClockRepository& aRepository)
    : m_world(aWorld)
    , m_repository(aRepository)
{
    m_updateConnection = aDispatcher.sink<UpdateEvent>().connect<&CalendarService::OnUpdate>(this);
    m_joinConnection = aDispatcher.sink<PlayerJoinEvent>().connect<&CalendarService::OnPlayerJoin>(this);
}

void CalendarService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (acEvent.Delta > 0.f)
        m_secondsSinceSave += acEvent.Delta;
    if (m_secondsSinceSave >= kClockSaveIntervalSeconds)
        SaveClock();

    if (!m_lastTick)
        m_lastTick = GameServer::Get()->GetTick();

    auto now = GameServer::Get()->GetTick();

    // client got ahead, we wait
    if (now < m_lastTick)
        return;

    auto delta = now - m_lastTick;
    m_lastTick = now;
    m_dateTime.Update(delta);
}

void CalendarService::OnPlayerJoin(const PlayerJoinEvent& acEvent) noexcept
{
    ServerTimeSettings timeMsg;
    // the player with the furthest date is used
    bool playerHasFurthestTime = acEvent.PlayerTime.GetTimeInDays() > m_dateTime.GetTimeInDays();

    if (playerHasFurthestTime)
    {
        // Note that this doesn't set timescale because the server config should set that.
        if (!m_timeInitialized)
        {
            m_dateTime.m_timeModel.Time = acEvent.PlayerTime.m_timeModel.Time;
            m_timeInitialized = true;
        }
        m_dateTime.m_timeModel.Day = acEvent.PlayerTime.m_timeModel.Day;
        m_dateTime.m_timeModel.Month = acEvent.PlayerTime.m_timeModel.Month;
        m_dateTime.m_timeModel.Year = acEvent.PlayerTime.m_timeModel.Year;
    }
    timeMsg.timeModel.TimeScale = m_dateTime.m_timeModel.TimeScale;
    timeMsg.timeModel.Time = m_dateTime.m_timeModel.Time;
    timeMsg.timeModel.Day = m_dateTime.m_timeModel.Day;
    timeMsg.timeModel.Month = m_dateTime.m_timeModel.Month;
    timeMsg.timeModel.Year = m_dateTime.m_timeModel.Year;

    if (playerHasFurthestTime)
        GameServer::Get()->SendToPlayers(timeMsg);
    else
        acEvent.pPlayer->Send(timeMsg);

    const auto [hour, minute] = GetTime();
    spdlog::info("[CalendarService] Sent server clock {:02}:{:02}, timescale {}, to player {}", hour, minute,
        m_dateTime.m_timeModel.TimeScale, acEvent.pPlayer->GetId());
}

bool CalendarService::SetTime(int aHours, int aMinutes, float aScale) noexcept
{
    m_dateTime.m_timeModel.TimeScale = aScale;

    if (aHours >= 0 && aHours <= 23 && aMinutes >= 0 && aMinutes <= 59)
    {
        // encode time as skyrim time
        auto minutes = static_cast<float>(aMinutes) * 0.17f;
        minutes = floor(minutes * 100) / 1000;
        m_dateTime.m_timeModel.Time = static_cast<float>(aHours) + minutes;
        m_timeInitialized = true;

        SendTimeResync();

        GameServer::Get()->GetWorld().GetScriptService().HandleSetTime(aHours, aMinutes, aScale);
        return true;
    }
    return false;
}

bool CalendarService::SetDate(int aDay, int aMonth, float aYear) noexcept
{
    if (aMonth >= 0 && aMonth < 12 && aYear >= 0 && aYear <= 999)
    {
        auto maxDays = m_dateTime.GetNumberOfDaysByMonthIndex(aMonth);
        if (aDay >= 0 && aDay < maxDays)
        {
            m_dateTime.m_timeModel.Day = aDay;
            m_dateTime.m_timeModel.Month = aMonth;
            m_dateTime.m_timeModel.Year = aYear;
            SendTimeResync();
            return true;
        }
    }
    return false;
}

void CalendarService::SendTimeResync() noexcept
{
    ServerTimeSettings timeMsg;
    timeMsg.timeModel.TimeScale = m_dateTime.m_timeModel.TimeScale;
    timeMsg.timeModel.Time = m_dateTime.m_timeModel.Time;
    timeMsg.timeModel.Day = m_dateTime.m_timeModel.Day;
    timeMsg.timeModel.Month = m_dateTime.m_timeModel.Month;
    timeMsg.timeModel.Year = m_dateTime.m_timeModel.Year;
    GameServer::Get()->SendToLoaded(timeMsg);
}

CalendarService::TTime CalendarService::GetTime() const noexcept
{
    const auto hour = floor(m_dateTime.m_timeModel.Time);
    const auto minutes = (m_dateTime.m_timeModel.Time - hour) / 17.f;

    const auto flatMinutes = static_cast<int>(ceil((minutes * 100.f) * 10.f));
    return {static_cast<int>(hour), flatMinutes};
}

CalendarService::TTime CalendarService::GetRealTime() noexcept
{
    const auto t = std::time(nullptr);
    int h = (t / 3600) % 24;
    int m = (t / 60) % 60;
    return {h, m};
}

CalendarService::TDate CalendarService::GetDate() const noexcept
{
    return {m_dateTime.m_timeModel.Day, m_dateTime.m_timeModel.Month, m_dateTime.m_timeModel.Year};
}

bool CalendarService::SetTimeScale(float aScale) noexcept
{
    if (aScale >= 0.f && aScale <= 1000.f)
    {
        m_dateTime.m_timeModel.TimeScale = aScale;

        ServerTimeSettings timeMsg;
        timeMsg.timeModel.TimeScale = m_dateTime.m_timeModel.TimeScale;
        timeMsg.timeModel.Time = m_dateTime.m_timeModel.Time;
        timeMsg.timeModel.Day = m_dateTime.m_timeModel.Day;
        timeMsg.timeModel.Month = m_dateTime.m_timeModel.Month;
        timeMsg.timeModel.Year = m_dateTime.m_timeModel.Year;
        GameServer::Get()->SendToPlayers(timeMsg);
        return true;
    }

    return false;
}

bool CalendarService::RestoreSavedClock() noexcept
{
    std::optional<Persistence::WorldClockRecord> saved;
    try
    {
        saved = m_repository.Load();
    }
    catch (const std::exception& acException)
    {
        spdlog::error("[CalendarService] Failed to load the saved clock: {}", acException.what());
        return false;
    }

    if (!saved)
        return false;

    m_dateTime.m_timeModel.Time = saved->Time;
    m_dateTime.m_timeModel.Day = saved->Day;
    m_dateTime.m_timeModel.Month = saved->Month;
    m_dateTime.m_timeModel.Year = saved->Year;
    m_timeInitialized = true;
    SendTimeResync();

    const auto [hour, minute] = GetTime();
    spdlog::info("[CalendarService] Restored saved clock {:02}:{:02} {}/{}/{}", hour, minute, saved->Day, saved->Month, saved->Year);
    return true;
}

void CalendarService::SaveClock() noexcept
{
    m_secondsSinceSave = 0.f;

    Persistence::WorldClockRecord record{};
    record.Time = m_dateTime.m_timeModel.Time;
    record.Day = m_dateTime.m_timeModel.Day;
    record.Month = m_dateTime.m_timeModel.Month;
    record.Year = m_dateTime.m_timeModel.Year;
    try
    {
        (void)m_repository.Save(record);
    }
    catch (const std::exception& acException)
    {
        spdlog::error("[CalendarService] Failed to save the clock: {}", acException.what());
    }
}
