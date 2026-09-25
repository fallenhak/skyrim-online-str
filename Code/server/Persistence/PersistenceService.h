#pragma once

#include <Persistence/CharacterRepository.h>
#include <Persistence/RenewableEncounterRepository.h>
#include <Persistence/WorldClockRepository.h>
#include <Persistence/WorldObjectRepository.h>

#include <filesystem>

struct PersistenceService final
{
    explicit PersistenceService(std::filesystem::path aDatabasePath = DefaultDatabasePath());
    ~PersistenceService() noexcept = default;

    PersistenceService(const PersistenceService&) = delete;
    PersistenceService& operator=(const PersistenceService&) = delete;
    PersistenceService(PersistenceService&&) = delete;
    PersistenceService& operator=(PersistenceService&&) = delete;

    [[nodiscard]] Persistence::CharacterRepository& GetCharacterRepository() noexcept { return m_characterRepository; }
    [[nodiscard]] const Persistence::CharacterRepository& GetCharacterRepository() const noexcept { return m_characterRepository; }
    [[nodiscard]] Persistence::RenewableEncounterRepository& GetRenewableEncounterRepository() noexcept { return m_renewableEncounterRepository; }
    [[nodiscard]] Persistence::WorldObjectRepository& GetWorldObjectRepository() noexcept { return m_worldObjectRepository; }
    [[nodiscard]] Persistence::WorldClockRepository& GetWorldClockRepository() noexcept { return m_worldClockRepository; }
    [[nodiscard]] const std::filesystem::path& GetDatabasePath() const noexcept { return m_database.GetPath(); }

    [[nodiscard]] static std::filesystem::path DefaultDatabasePath();

private:
    Persistence::Database m_database;
    Persistence::CharacterRepository m_characterRepository;
    Persistence::RenewableEncounterRepository m_renewableEncounterRepository;
    Persistence::WorldObjectRepository m_worldObjectRepository;
    Persistence::WorldClockRepository m_worldClockRepository;
};
