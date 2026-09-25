#include <Persistence/WorldClockRepository.h>

#include <gtest/gtest.h>

#include <limits>

namespace
{
using Persistence::WorldClockRecord;

WorldClockRecord MakeClock(const float aTime, const std::uint32_t aDay, const std::uint32_t aMonth, const std::uint32_t aYear)
{
    WorldClockRecord record{};
    record.Time = aTime;
    record.Day = aDay;
    record.Month = aMonth;
    record.Year = aYear;
    return record;
}
} // namespace

TEST(PersistenceWorldClockRepository, StartsEmptyThenKeepsTheLatestClock)
{
    Persistence::Database database(":memory:");
    database.Migrate();
    Persistence::WorldClockRepository repository(database);

    EXPECT_FALSE(repository.Load().has_value());

    ASSERT_TRUE(repository.Save(MakeClock(13.28f, 17, 7, 201)));
    ASSERT_TRUE(repository.Save(MakeClock(14.05f, 18, 7, 201)));

    const auto loaded = repository.Load();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_FLOAT_EQ(loaded->Time, 14.05f);
    EXPECT_EQ(loaded->Day, 18u);
    EXPECT_EQ(loaded->Month, 7u);
    EXPECT_EQ(loaded->Year, 201u);

    auto rows = database.Prepare("SELECT COUNT(*) FROM world_clock;");
    ASSERT_TRUE(rows.Step());
    EXPECT_EQ(rows.ColumnInt64(0), 1);
}

TEST(PersistenceWorldClockRepository, RejectsAnOutOfRangeClockAndKeepsTheStoredOne)
{
    Persistence::Database database(":memory:");
    database.Migrate();
    Persistence::WorldClockRepository repository(database);
    ASSERT_TRUE(repository.Save(MakeClock(9.5f, 1, 0, 1)));

    EXPECT_FALSE(repository.Save(MakeClock(24.f, 1, 0, 1)));
    EXPECT_FALSE(repository.Save(MakeClock(-0.1f, 1, 0, 1)));
    EXPECT_FALSE(repository.Save(MakeClock(std::numeric_limits<float>::quiet_NaN(), 1, 0, 1)));
    EXPECT_FALSE(repository.Save(MakeClock(9.f, 1, 12, 1)));
    EXPECT_FALSE(repository.Save(MakeClock(9.f, 32, 0, 1)));
    EXPECT_FALSE(repository.Save(MakeClock(9.f, 1, 0, 1000)));

    const auto loaded = repository.Load();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_FLOAT_EQ(loaded->Time, 9.5f);
}

TEST(PersistenceWorldClockRepository, MigratesAVersionFiveDatabase)
{
    Persistence::Database database(":memory:");
    database.Execute("CREATE TABLE schema_version (id INTEGER PRIMARY KEY CHECK (id = 1), version INTEGER NOT NULL);");
    database.Execute("INSERT INTO schema_version (id, version) VALUES (1, 5);");

    database.Migrate();

    auto version = database.Prepare("SELECT version FROM schema_version WHERE id = 1;");
    ASSERT_TRUE(version.Step());
    EXPECT_EQ(version.ColumnInt64(0), 6);

    Persistence::WorldClockRepository repository(database);
    EXPECT_FALSE(repository.Load().has_value());
    EXPECT_TRUE(repository.Save(MakeClock(12.f, 1, 1, 1)));
}
