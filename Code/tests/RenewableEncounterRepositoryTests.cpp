#include <Persistence/RenewableEncounterRepository.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace
{
using Persistence::RenewableEncounterRecord;

RenewableEncounterRecord MakeRecord(const std::uint32_t aCell, const std::uint32_t aGroup, const std::uint64_t aEpoch, const bool aCleared, const std::uint64_t aCooldown)
{
    RenewableEncounterRecord record{};
    record.CellFormId = aCell;
    record.GroupIndex = aGroup;
    record.Epoch = aEpoch;
    record.Cleared = aCleared;
    record.CooldownRemainingTicks = aCooldown;
    return record;
}

void ExpectSame(const RenewableEncounterRecord& acExpected, const RenewableEncounterRecord& acActual)
{
    EXPECT_EQ(acActual.CellFormId, acExpected.CellFormId);
    EXPECT_EQ(acActual.GroupIndex, acExpected.GroupIndex);
    EXPECT_EQ(acActual.Epoch, acExpected.Epoch);
    EXPECT_EQ(acActual.Cleared, acExpected.Cleared);
    EXPECT_EQ(acActual.CooldownRemainingTicks, acExpected.CooldownRemainingTicks);
}
} // namespace

TEST(PersistenceRenewableEncounterRepository, StartsEmptyAndRoundTripsTheMinimumState)
{
    Persistence::Database database(":memory:");
    database.Migrate();
    database.Migrate();
    Persistence::RenewableEncounterRepository repository(database);

    EXPECT_TRUE(repository.LoadAll().empty());

    const std::vector<RenewableEncounterRecord> records{
        MakeRecord(0x0001A2B3u, 1, 7, true, 70),
        MakeRecord(0x0001A2B3u, 0, 0, false, 0),
        MakeRecord(0xFFFFFFFFu, 0xFFFFFFFFu, static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()), true, 0),
    };
    ASSERT_TRUE(repository.SaveAll(records));

    const auto loaded = repository.LoadAll();
    ASSERT_EQ(loaded.size(), 3u);
    // Ordered by encounter id, independent of save order.
    ExpectSame(records[1], loaded[0]);
    ExpectSame(records[0], loaded[1]);
    ExpectSame(records[2], loaded[2]);
}

TEST(PersistenceRenewableEncounterRepository, SaveReplacesThePreviousSnapshot)
{
    Persistence::Database database(":memory:");
    database.Migrate();
    Persistence::RenewableEncounterRepository repository(database);

    ASSERT_TRUE(repository.SaveAll({MakeRecord(1, 0, 0, false, 0), MakeRecord(2, 0, 3, true, 10)}));
    ASSERT_TRUE(repository.SaveAll({MakeRecord(2, 0, 4, false, 0)}));

    const auto loaded = repository.LoadAll();
    ASSERT_EQ(loaded.size(), 1u);
    ExpectSame(MakeRecord(2, 0, 4, false, 0), loaded[0]);

    ASSERT_TRUE(repository.SaveAll({}));
    EXPECT_TRUE(repository.LoadAll().empty());
}

TEST(PersistenceRenewableEncounterRepository, RejectsAMalformedSnapshotWithoutTouchingTheStoredOne)
{
    Persistence::Database database(":memory:");
    database.Migrate();
    Persistence::RenewableEncounterRepository repository(database);
    ASSERT_TRUE(repository.SaveAll({MakeRecord(1, 0, 5, true, 20)}));

    const auto tooLarge = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1u;
    EXPECT_FALSE(repository.SaveAll({MakeRecord(0, 0, 0, false, 0)}));                          // invalid cell
    EXPECT_FALSE(repository.SaveAll({MakeRecord(3, 0, tooLarge, false, 0)}));                   // epoch overflows SQLite
    EXPECT_FALSE(repository.SaveAll({MakeRecord(3, 0, 0, true, tooLarge)}));                    // cooldown overflows SQLite
    EXPECT_FALSE(repository.SaveAll({MakeRecord(3, 0, 0, false, 5)}));                          // live encounter with a cooldown
    EXPECT_FALSE(repository.SaveAll({MakeRecord(3, 0, 0, false, 0), MakeRecord(3, 0, 1, false, 0)})); // duplicate id

    const auto loaded = repository.LoadAll();
    ASSERT_EQ(loaded.size(), 1u);
    ExpectSame(MakeRecord(1, 0, 5, true, 20), loaded[0]);
}

TEST(PersistenceRenewableEncounterRepository, SkipsRowsThatWereCorruptedOnDisk)
{
    Persistence::Database database(":memory:");
    database.Migrate();
    Persistence::RenewableEncounterRepository repository(database);
    ASSERT_TRUE(repository.SaveAll({MakeRecord(1, 0, 2, false, 0)}));

    // The schema CHECKs refuse bad rows even when written around the repository.
    EXPECT_THROW(database.Execute("INSERT INTO renewable_encounters VALUES (0, 0, 0, 0, 0, 0);"), std::runtime_error);
    EXPECT_THROW(database.Execute("INSERT INTO renewable_encounters VALUES (5, 0, -1, 0, 0, 0);"), std::runtime_error);
    EXPECT_THROW(database.Execute("INSERT INTO renewable_encounters VALUES (5, 0, 0, 2, 0, 0);"), std::runtime_error);
    EXPECT_THROW(database.Execute("INSERT INTO renewable_encounters VALUES (4294967296, 0, 0, 0, 0, 0);"), std::runtime_error);

    const auto loaded = repository.LoadAll();
    ASSERT_EQ(loaded.size(), 1u);
    ExpectSame(MakeRecord(1, 0, 2, false, 0), loaded[0]);
}

TEST(PersistenceRenewableEncounterRepository, PersistsAfterReopeningAnOnDiskDatabase)
{
    const auto path = std::filesystem::temp_directory_path() /
                      ("renewable_encounters_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite");
    {
        Persistence::Database database(path);
        database.Migrate();
        Persistence::RenewableEncounterRepository repository(database);
        ASSERT_TRUE(repository.SaveAll({MakeRecord(0x0001A2B4u, 0, 9, true, 1800)}));
    }
    {
        Persistence::Database database(path);
        database.Migrate();
        Persistence::RenewableEncounterRepository repository(database);
        const auto loaded = repository.LoadAll();
        ASSERT_EQ(loaded.size(), 1u);
        ExpectSame(MakeRecord(0x0001A2B4u, 0, 9, true, 1800), loaded[0]);
    }

    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path.string() + "-wal", error);
    std::filesystem::remove(path.string() + "-shm", error);
}
