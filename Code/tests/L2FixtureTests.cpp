#include <fixture/L2Fixture.h>

#include <ESLoader.h>
#include <RecordCollection.h>
#include <Records/TESFileRecordTypes.inl>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

#include <spdlog/sinks/ostream_sink.h>

// Round trip: the L2 bot fixture plugin written from bytes, read back by our own parser (#91 step 1).
namespace
{
class FixtureDataDirectory
{
public:
    FixtureDataDirectory()
    {
        const auto uniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() / ("skyrim-online-str-l2-fixture-" + std::to_string(uniqueSuffix));
        m_written = L2Fixture::WriteDataDirectory(m_path);
    }

    ~FixtureDataDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    [[nodiscard]] bool Written() const noexcept { return m_written; }
    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
    bool m_written{};
};

class L2FixtureTest : public ::testing::Test
{
public:
    static void SetUpTestSuite()
    {
        s_directory = std::make_unique<FixtureDataDirectory>();
        ASSERT_TRUE(s_directory->Written());

        // Any parser warning means the fixture and the parser disagree about the format.
        auto previousLogger = spdlog::default_logger();
        auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(s_warnings);
        sink->set_level(spdlog::level::warn);
        auto logger = std::make_shared<spdlog::logger>("l2-fixture", sink);
        logger->set_level(spdlog::level::trace);
        spdlog::set_default_logger(logger);

        ESLoader::ESLoader loader(s_directory->Path());
        s_collection = loader.BuildRecordCollection(true);
        s_loadOrder = loader.GetLoadOrder();

        spdlog::set_default_logger(previousLogger);
    }

    static void TearDownTestSuite()
    {
        s_collection.reset();
        s_directory.reset();
    }

    static inline std::unique_ptr<FixtureDataDirectory> s_directory;
    static inline UniquePtr<ESLoader::RecordCollection> s_collection;
    static inline ESLoader::PluginCollection s_loadOrder;
    static inline std::ostringstream s_warnings;
};

TEST_F(L2FixtureTest, LoadsWithoutWarnings)
{
    ASSERT_TRUE(s_collection);
    EXPECT_TRUE(s_collection->HasAnyRecords());
    EXPECT_EQ(s_warnings.str(), "");

    ASSERT_EQ(s_loadOrder.size(), 1u);
    EXPECT_EQ(s_loadOrder[0].m_filename, L2Fixture::kPluginName);
    EXPECT_FALSE(s_loadOrder[0].IsLite());
    EXPECT_EQ(s_loadOrder[0].m_standardId, 0);
}

TEST_F(L2FixtureTest, RecordsWithoutParserKeepTheirType)
{
    ASSERT_TRUE(s_collection);
    EXPECT_EQ(static_cast<uint32_t>(s_collection->GetFormType(L2Fixture::kDoor)), 0x524F4F44u);  // DOOR
    EXPECT_EQ(static_cast<uint32_t>(s_collection->GetFormType(L2Fixture::kLever)), 0x49544341u); // ACTI
    EXPECT_EQ(static_cast<uint32_t>(s_collection->GetFormType(L2Fixture::kKey)), 0x4D59454Bu);   // KEYM
    EXPECT_EQ(s_collection->GetFormType(L2Fixture::kChest), FormEnum::CONT);
}

TEST_F(L2FixtureTest, EncounterZoneAndCell)
{
    ASSERT_TRUE(s_collection);
    const ECZN* pZone = s_collection->FindEncounterZoneById(L2Fixture::kEncounterZone);
    ASSERT_NE(pZone, nullptr);
    EXPECT_EQ(pZone->m_editorId, "L2FixtureZone");
    EXPECT_EQ(pZone->m_minLevel, L2Fixture::kZoneMinLevel);
    EXPECT_EQ(pZone->m_maxLevel, L2Fixture::kZoneMaxLevel);

    const CELL* pCell = s_collection->FindCellById(L2Fixture::kCell);
    ASSERT_NE(pCell, nullptr);
    EXPECT_EQ(pCell->m_encounterZone, L2Fixture::kEncounterZone);
}

TEST_F(L2FixtureTest, ContainerAndLeveledItem)
{
    ASSERT_TRUE(s_collection);
    const CONT* pChest = s_collection->FindContainerById(L2Fixture::kChest);
    ASSERT_NE(pChest, nullptr);
    EXPECT_EQ(pChest->m_editorId, "L2FixtureChest");
    ASSERT_EQ(pChest->m_objects.size(), 2u);
    EXPECT_EQ(pChest->m_objects[0].m_formId, L2Fixture::kLootList);
    EXPECT_EQ(pChest->m_objects[0].m_count, 1u);
    EXPECT_EQ(pChest->m_objects[1].m_formId, L2Fixture::kGold);
    EXPECT_EQ(pChest->m_objects[1].m_count, L2Fixture::kChestGold);

    const LVLI* pLoot = s_collection->FindLeveledItemById(L2Fixture::kLootList);
    ASSERT_NE(pLoot, nullptr);
    EXPECT_EQ(pLoot->m_editorId, "L2FixtureLoot");
    EXPECT_EQ(pLoot->m_flags, LVLI::kCalculateFromAllLevels);
    ASSERT_EQ(pLoot->m_entries.size(), 2u);
    EXPECT_EQ(pLoot->m_entries[0].Level, 1);
    EXPECT_EQ(pLoot->m_entries[0].FormId, L2Fixture::kGold);
    EXPECT_EQ(pLoot->m_entries[0].Count, 5);
    EXPECT_EQ(pLoot->m_entries[1].Level, 10);
    EXPECT_EQ(pLoot->m_entries[1].FormId, L2Fixture::kKey);
}

TEST_F(L2FixtureTest, LeveledActor)
{
    ASSERT_TRUE(s_collection);
    const NPC* pBandit = s_collection->FindNpcById(L2Fixture::kBandit);
    ASSERT_NE(pBandit, nullptr);
    EXPECT_EQ(pBandit->m_editorId, "L2FixtureBandit");
    EXPECT_EQ(pBandit->m_raceId, L2Fixture::kRace);
    ASSERT_NE(s_collection->FindRaceById(L2Fixture::kRace), nullptr);

    const LVLN* pList = s_collection->FindLeveledNpcById(L2Fixture::kBanditList);
    ASSERT_NE(pList, nullptr);
    ASSERT_EQ(pList->m_entries.size(), 1u);
    EXPECT_EQ(pList->m_entries[0].FormId, L2Fixture::kBandit);

    const ACHR* pActor = s_collection->FindActorReferenceById(L2Fixture::kBanditRef);
    ASSERT_NE(pActor, nullptr);
    EXPECT_EQ(pActor->m_baseObject.m_baseId, L2Fixture::kBanditList);
    EXPECT_EQ(pActor->m_parentCell, L2Fixture::kCell);
}

TEST_F(L2FixtureTest, PlacedReferences)
{
    ASSERT_TRUE(s_collection);
    const REFR* pChest = s_collection->FindObjectRefById(L2Fixture::kChestRef);
    ASSERT_NE(pChest, nullptr);
    EXPECT_EQ(pChest->m_basicObject.m_baseId, L2Fixture::kChest);
    EXPECT_EQ(pChest->m_parentCell, L2Fixture::kCell);
    EXPECT_TRUE(pChest->m_isLocked);
    EXPECT_EQ(pChest->m_lockLevel, L2Fixture::kChestLockLevel);
    EXPECT_EQ(pChest->m_lockKey, L2Fixture::kKey);

    for (const uint32_t id : {L2Fixture::kDoorRef, L2Fixture::kLeverRef})
    {
        const REFR* pReference = s_collection->FindObjectRefById(id);
        ASSERT_NE(pReference, nullptr) << std::hex << id;
        EXPECT_EQ(pReference->m_parentCell, L2Fixture::kCell);
        EXPECT_FALSE(pReference->m_isLocked);
    }

    const REFR* pLoot = s_collection->FindObjectRefById(L2Fixture::kPlacedLootRef);
    ASSERT_NE(pLoot, nullptr);
    EXPECT_EQ(pLoot->m_basicObject.m_baseId, L2Fixture::kDummyItem);
    EXPECT_EQ(pLoot->m_leveledItemBase, L2Fixture::kLootList);
}
} // namespace
