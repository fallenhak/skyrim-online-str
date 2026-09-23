#include <Services/ActorPopulationPolicy.h>
#include <Services/ActorPopulationIdentityResolver.h>
#include <Services/ActorPopulationAssignmentPolicy.h>

#define TP_INTERNAL_COMPONENTS_GUARD
#include <Components/ModsComponent.h>
#undef TP_INTERNAL_COMPONENTS_GUARD
#include <ESLoader.h>
#include <RecordCollection.h>
#include <Records/TESFileRecordTypes.inl>
#include <TESFile.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
using Bytes = std::vector<uint8_t>;

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        const auto uniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("skyrim-online-str-load-order-test-" + std::to_string(uniqueSuffix));
        std::filesystem::create_directory(m_path, m_error);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }

    [[nodiscard]] bool IsCreated() const noexcept
    {
        std::error_code error;
        return !m_error && std::filesystem::is_directory(m_path, error) && !error;
    }
    [[nodiscard]] const std::error_code& Error() const noexcept { return m_error; }
    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
    std::error_code m_error;
};

constexpr uint32_t kMasterPrefix = 0x02000000;
constexpr uint32_t kNordRaceRawId = 0x01001000;
constexpr uint32_t kWolfRaceRawId = 0x01001100;
constexpr uint32_t kDraugrRaceRawId = 0x01001200;
constexpr uint32_t kEdgeRaceRawId = 0x01001300;
constexpr uint32_t kNoEditorRaceRawId = 0x01001400;
constexpr uint32_t kBretonRaceRawId = 0x01001500;

constexpr uint32_t kNordNpcRawId = 0x01002000;
constexpr uint32_t kWolfNpcRawId = 0x01002100;
constexpr uint32_t kDraugrNpcRawId = 0x01002200;
constexpr uint32_t kEdgeNpcRawId = 0x01002300;
constexpr uint32_t kMissingRnamNpcRawId = 0x01002400;
constexpr uint32_t kMissingRaceNpcRawId = 0x01002500;
constexpr uint32_t kNoEditorNpcRawId = 0x01002600;
constexpr uint32_t kMasterNpcRawId = 0x01002700;
constexpr uint32_t kBretonNpcRawId = 0x01002800;

constexpr uint32_t kNordActorReferenceRawId = 0x01003000;
constexpr uint32_t kWolfActorReferenceRawId = 0x01003100;
constexpr uint32_t kMissingNpcActorReferenceRawId = 0x01003200;
constexpr uint32_t kMasterActorReferenceRawId = 0x01003300;

constexpr uint32_t kNordRaceId = kNordRaceRawId;
constexpr uint32_t kWolfRaceId = kWolfRaceRawId;
constexpr uint32_t kDraugrRaceId = kDraugrRaceRawId;
constexpr uint32_t kEdgeRaceId = kEdgeRaceRawId;
constexpr uint32_t kMasterRaceRawId = 0x00003000;
constexpr uint32_t kMasterRaceId = kMasterPrefix + kMasterRaceRawId;

constexpr uint32_t kNordNpcId = kNordNpcRawId;
constexpr uint32_t kWolfNpcId = kWolfNpcRawId;
constexpr uint32_t kDraugrNpcId = kDraugrNpcRawId;
constexpr uint32_t kEdgeNpcId = kEdgeNpcRawId;
constexpr uint32_t kMissingRnamNpcId = kMissingRnamNpcRawId;
constexpr uint32_t kMissingRaceNpcId = kMissingRaceNpcRawId;
constexpr uint32_t kNoEditorNpcId = kNoEditorNpcRawId;
constexpr uint32_t kMasterNpcId = kMasterNpcRawId;
constexpr uint32_t kBretonNpcId = kBretonNpcRawId;
constexpr uint32_t kNordActorReferenceId = kNordActorReferenceRawId;
constexpr uint32_t kWolfActorReferenceId = kWolfActorReferenceRawId;
constexpr uint32_t kMissingNpcActorReferenceId = kMissingNpcActorReferenceRawId;
constexpr uint32_t kMasterActorReferenceId = kMasterActorReferenceRawId;
constexpr uint32_t kMasterActorBaseId = kMasterPrefix + 0x00002700;

template <typename T>
void AppendValue(Bytes& aBytes, T aValue)
{
    const auto* const pValue = reinterpret_cast<const uint8_t*>(&aValue);
    aBytes.insert(aBytes.end(), pValue, pValue + sizeof(T));
}

void AppendChunk(Bytes& aBytes, ChunkId aChunkId, const Bytes& aPayload)
{
    AppendValue(aBytes, static_cast<uint32_t>(aChunkId));
    AppendValue(aBytes, static_cast<uint16_t>(aPayload.size()));
    aBytes.insert(aBytes.end(), aPayload.begin(), aPayload.end());
}

void AppendEditorId(Bytes& aBytes, const char* apEditorId)
{
    Bytes payload(apEditorId, apEditorId + std::char_traits<char>::length(apEditorId));
    payload.push_back(0);
    AppendChunk(aBytes, ChunkId::EDID_ID, payload);
}

Bytes MakeRaceData(const char* apEditorId)
{
    Bytes data;
    if (apEditorId != nullptr)
        AppendEditorId(data, apEditorId);
    return data;
}

Bytes MakeNpcData(const char* apEditorId, const uint32_t* apRaceRawId)
{
    Bytes data;
    AppendEditorId(data, apEditorId);

    if (apRaceRawId != nullptr)
    {
        Bytes payload;
        AppendValue(payload, *apRaceRawId);
        AppendChunk(data, ChunkId::RNAM_ID, payload);
    }

    return data;
}

Bytes MakeActorReferenceData(const uint32_t aBaseRawId)
{
    Bytes data;
    Bytes payload;
    AppendValue(payload, aBaseRawId);
    AppendChunk(data, ChunkId::NAME_ID, payload);
    return data;
}

void AppendRecord(Bytes& aBytes, FormEnum aFormType, uint32_t aFormId, const Bytes& aData, const uint32_t aFlags = 0)
{
    AppendValue(aBytes, static_cast<uint32_t>(aFormType));
    AppendValue(aBytes, static_cast<uint32_t>(aData.size()));
    AppendValue(aBytes, aFlags);
    AppendValue(aBytes, aFormId);
    AppendValue(aBytes, uint32_t{}); // version control info
    AppendValue(aBytes, uint16_t{}); // form version
    AppendValue(aBytes, uint16_t{}); // version control version
    aBytes.insert(aBytes.end(), aData.begin(), aData.end());
}

Bytes MakePluginHeader(const uint32_t aFlags)
{
    Bytes data;
    AppendRecord(data, FormEnum::TES4, 0, {}, aFlags);
    return data;
}

Bytes MakePluginHeaderWithMasters(std::initializer_list<const char*> acMasterFilenames)
{
    Bytes headerData;
    for (const char* pMasterFilename : acMasterFilenames)
    {
        Bytes masterName(pMasterFilename, pMasterFilename + std::char_traits<char>::length(pMasterFilename));
        masterName.push_back(0);
        AppendChunk(headerData, ChunkId::MAST_ID, masterName);
        AppendChunk(headerData, ChunkId::DATA_ID, Bytes(sizeof(uint64_t), 0));
    }

    Bytes data;
    AppendRecord(data, FormEnum::TES4, 0, headerData);
    return data;
}

Bytes MakePluginHeaderWithMaster(const char* apMasterFilename)
{
    return MakePluginHeaderWithMasters({apMasterFilename});
}

Bytes MakePluginData()
{
    Bytes data;
    Bytes headerData;
    Bytes masterName("Master.esm", "Master.esm" + std::char_traits<char>::length("Master.esm"));
    masterName.push_back(0);
    AppendChunk(headerData, ChunkId::MAST_ID, masterName);
    AppendRecord(data, FormEnum::TES4, 0, headerData);

    AppendRecord(data, FormEnum::RACE, kMasterRaceRawId, MakeRaceData("MasterRace"));
    AppendRecord(data, FormEnum::RACE, kNordRaceRawId, MakeRaceData("NordRace"));
    AppendRecord(data, FormEnum::RACE, kWolfRaceRawId, MakeRaceData("WolfRace"));
    AppendRecord(data, FormEnum::RACE, kDraugrRaceRawId, MakeRaceData("DraugrRace"));
    AppendRecord(data, FormEnum::RACE, kEdgeRaceRawId, MakeRaceData("EdgeRace"));
    AppendRecord(data, FormEnum::RACE, kNoEditorRaceRawId, MakeRaceData(nullptr));
    AppendRecord(data, FormEnum::RACE, kBretonRaceRawId, MakeRaceData("BretonRace"));

    AppendRecord(data, FormEnum::NPC_, kNordNpcRawId, MakeNpcData("NordNpc", &kNordRaceRawId));
    AppendRecord(data, FormEnum::NPC_, kWolfNpcRawId, MakeNpcData("WolfNpc", &kWolfRaceRawId));
    AppendRecord(data, FormEnum::NPC_, kDraugrNpcRawId, MakeNpcData("DraugrNpc", &kDraugrRaceRawId));
    AppendRecord(data, FormEnum::NPC_, kEdgeNpcRawId, MakeNpcData("EdgeNpc", &kEdgeRaceRawId));
    AppendRecord(data, FormEnum::NPC_, kMissingRnamNpcRawId, MakeNpcData("MissingRnamNpc", nullptr));

    constexpr uint32_t missingRaceRawId = 0x00001FFF;
    AppendRecord(data, FormEnum::NPC_, kMissingRaceNpcRawId, MakeNpcData("MissingRaceNpc", &missingRaceRawId));
    AppendRecord(data, FormEnum::NPC_, kNoEditorNpcRawId, MakeNpcData("NoEditorNpc", &kNoEditorRaceRawId));
    AppendRecord(data, FormEnum::NPC_, kMasterNpcRawId, MakeNpcData("MasterRefNpc", &kMasterRaceRawId));
    AppendRecord(data, FormEnum::NPC_, kBretonNpcRawId, MakeNpcData("BretonNpc", &kBretonRaceRawId));

    AppendRecord(data, FormEnum::ACHR, kNordActorReferenceRawId, MakeActorReferenceData(kNordNpcRawId));
    AppendRecord(data, FormEnum::ACHR, kWolfActorReferenceRawId, MakeActorReferenceData(kWolfNpcRawId));
    AppendRecord(data, FormEnum::ACHR, kMissingNpcActorReferenceRawId, MakeActorReferenceData(0x01002FFF));
    AppendRecord(data, FormEnum::ACHR, kMasterActorReferenceRawId, MakeActorReferenceData(0x00002700));

    return data;
}

class ActorPopulationTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_pluginPath = std::filesystem::temp_directory_path() / "skyrim-online-str-actor-population-test.esp";
        std::error_code error;
        std::filesystem::remove(m_pluginPath, error);

        std::ofstream plugin(m_pluginPath, std::ios::binary);
        ASSERT_TRUE(plugin.good());
        const Bytes data = MakePluginData();
        plugin.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        plugin.close();

        TiltedPhoques::Map<TiltedPhoques::String, uint32_t> masterFiles;
        masterFiles.emplace("Master.esm", uint32_t{0x02000000});
        ESLoader::TESFile tesFile(masterFiles);
        tesFile.Setup(uint8_t{1});
        ASSERT_TRUE(tesFile.LoadFile(m_pluginPath));
        ASSERT_TRUE(tesFile.IndexRecords(m_records));
    }

    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove(m_pluginPath, error);
    }

    ESLoader::RecordCollection m_records;
    std::filesystem::path m_pluginPath;
};

TEST_F(ActorPopulationTests, ResolvesNpcRaceAndIndexesRaceRecord)
{
    const auto* const pNpc = m_records.FindNpcById(kNordNpcId);
    ASSERT_NE(pNpc, nullptr);
    EXPECT_EQ(pNpc->m_raceId, kNordRaceId);

    const auto* const pRace = m_records.FindRaceById(kNordRaceId);
    ASSERT_NE(pRace, nullptr);
    EXPECT_EQ(pRace->m_editorId, "NordRace");

    const auto* const pMasterNpc = m_records.FindNpcById(kMasterNpcId);
    ASSERT_NE(pMasterNpc, nullptr);
    EXPECT_EQ(pMasterNpc->m_raceId, kMasterRaceId);
    EXPECT_NE(m_records.FindRaceById(kMasterRaceId), nullptr);

    const auto* const pNordActorReference = m_records.FindActorReferenceById(kNordActorReferenceId);
    ASSERT_NE(pNordActorReference, nullptr);
    EXPECT_EQ(pNordActorReference->m_baseObject.m_baseId, kNordNpcId);

    const auto* const pMasterActorReference = m_records.FindActorReferenceById(kMasterActorReferenceId);
    ASSERT_NE(pMasterActorReference, nullptr);
    EXPECT_EQ(pMasterActorReference->m_baseObject.m_baseId, kMasterActorBaseId);

    // Generic indexing keeps the existing plugin-local key behavior while the typed lookup
    // exposes the resolved server form ID used by classification.
    EXPECT_EQ(m_records.GetFormType(kNordRaceRawId), FormEnum::RACE);
}

TEST_F(ActorPopulationTests, MissingFindsDoNotInsertDefaultRecords)
{
    EXPECT_EQ(m_records.FindNpcById(0xDEADBEEF), nullptr);
    EXPECT_EQ(m_records.FindNpcById(0xDEADBEEF), nullptr);
    EXPECT_EQ(m_records.FindRaceById(0xDEADBEEF), nullptr);
    EXPECT_EQ(m_records.FindRaceById(0xDEADBEEF), nullptr);
    EXPECT_EQ(m_records.FindActorReferenceById(0xDEADBEEF), nullptr);

    const auto& constRecords = m_records;
    EXPECT_EQ(constRecords.FindNpcById(0xDEADBEEF), nullptr);
    EXPECT_EQ(constRecords.FindRaceById(0xDEADBEEF), nullptr);
    EXPECT_EQ(constRecords.FindActorReferenceById(0xDEADBEEF), nullptr);
}

TEST_F(ActorPopulationTests, ClassifiesPlayersAndConfiguredNpcRaces)
{
    ActorPopulationPolicy policy(&m_records);

    EXPECT_EQ(policy.ClassifyActor(GameId(0, 0x14)).Class, ActorPopulationClass::kPlayer);

    policy.SetRaceClassification("NordRace", ActorPopulationClass::kHumanoidNpc);
    policy.SetRaceClassification("WolfRace", ActorPopulationClass::kCreature);
    policy.SetRaceClassification("DraugrRace", ActorPopulationClass::kCreature);
    policy.SetRaceClassification("EdgeRace", ActorPopulationClass::kCreature);

    const auto humanoid = policy.ClassifyNpcBase(kNordNpcId);
    EXPECT_EQ(humanoid.Class, ActorPopulationClass::kHumanoidNpc);
    EXPECT_EQ(humanoid.NpcFormId, kNordNpcId);
    EXPECT_EQ(humanoid.RaceFormId, kNordRaceId);
    EXPECT_EQ(humanoid.RaceEditorId, "NordRace");

    EXPECT_EQ(policy.ClassifyNpcBase(kWolfNpcId).Class, ActorPopulationClass::kCreature);
    EXPECT_EQ(policy.ClassifyNpcBase(kDraugrNpcId).Class, ActorPopulationClass::kCreature);
    EXPECT_EQ(policy.ClassifyNpcBase(kEdgeNpcId).Class, ActorPopulationClass::kCreature);

    // The same parser data supports a later policy decision without parser changes.
    policy.SetRaceClassification("EdgeRace", ActorPopulationClass::kHumanoidNpc);
    EXPECT_EQ(policy.ClassifyNpcBase(kEdgeNpcId).Class, ActorPopulationClass::kHumanoidNpc);
}

TEST_F(ActorPopulationTests, InstallsConservativeVanillaHumanoidRules)
{
    ActorPopulationPolicy policy(&m_records);

    EXPECT_EQ(policy.ClassifyNpcBase(kNordNpcId).Class, ActorPopulationClass::kHumanoidNpc);
    EXPECT_EQ(policy.ClassifyNpcBase(kBretonNpcId).Class, ActorPopulationClass::kHumanoidNpc);
    EXPECT_EQ(policy.ClassifyNpcBase(kWolfNpcId).Class, ActorPopulationClass::kUnknown);
    EXPECT_EQ(policy.ClassifyNpcBase(kDraugrNpcId).Class, ActorPopulationClass::kUnknown);
    EXPECT_EQ(policy.ClassifyNpcBase(kEdgeNpcId).Class, ActorPopulationClass::kUnknown);
}

TEST(ActorPopulationPolicy, KeepsNpcUnknownWithoutLoadedRecords)
{
    ESLoader::RecordCollection records;
    ActorPopulationPolicy policy(&records);

    EXPECT_EQ(policy.ClassifyNpcBase(kNordNpcId).Class, ActorPopulationClass::kUnknown);
    EXPECT_EQ(policy.ClassifyActor(GameId(0, 0x14)).Class, ActorPopulationClass::kPlayer);
}

TEST(ESLoader, ParsesLoadOrderMetadataSafelyWithoutPluginFiles)
{
    TemporaryDirectory dataDirectory;
    ASSERT_TRUE(dataDirectory.IsCreated()) << dataDirectory.Error().message();

    {
        std::ofstream loadOrder(dataDirectory.Path() / "loadorder.txt", std::ios::binary);
        ASSERT_TRUE(loadOrder.good());
        loadOrder << "\xEF\xBB\xBF  Skyrim.esm  \r\n"
                  << " # comment\r\n"
                  << "\tUpdate.ESP\t\n"
                  << " \r\n"
                  << "Light.ESL\r\n"
                  << "skyrim.ESM\r\n"
                  << "../Escape.esp\r\n"
                  << "Nested/Plugin.esp\r\n"
                  << "Embedded\rPlugin.esp\r\n"
                  << "Malformed.xpm\r\n"
                  << "Not a plugin.txt\r\n";
    }

    ESLoader::ESLoader loader(dataDirectory.Path());
    const auto metadataOnly = loader.BuildRecordCollection(false);
    ASSERT_NE(metadataOnly, nullptr);

    const auto& plugins = loader.GetLoadOrder();
    ASSERT_EQ(plugins.size(), 3U);
    EXPECT_EQ(plugins[0].m_filename, "Skyrim.esm");
    EXPECT_FALSE(plugins[0].IsLite());
    EXPECT_EQ(plugins[0].m_standardId, 0U);
    EXPECT_EQ(plugins[1].m_filename, "Update.ESP");
    EXPECT_FALSE(plugins[1].IsLite());
    EXPECT_EQ(plugins[1].m_standardId, 1U);
    EXPECT_EQ(plugins[2].m_filename, "Light.ESL");
    EXPECT_TRUE(plugins[2].IsLite());
    EXPECT_EQ(plugins[2].m_liteId, 0U);

    // Metadata remains available even when record loading is explicitly enabled
    // and every listed plugin file is absent.
    const auto records = loader.BuildRecordCollection(true);
    ASSERT_NE(records, nullptr);
    EXPECT_FALSE(records->HasAnyRecords());
}

TEST(ESLoader, UsesTES4ESLFlagForLightPluginNamespace)
{
    TemporaryDirectory dataDirectory;
    ASSERT_TRUE(dataDirectory.IsCreated()) << dataDirectory.Error().message();

    {
        std::ofstream loadOrder(dataDirectory.Path() / "loadorder.txt");
        ASSERT_TRUE(loadOrder.good());
        loadOrder << "Flagged.esp\n"
                  << "Standard.esp\n"
                  << "Flagged.esm\n"
                  << "Standard.esm\n"
                  << "Flagged.esl\n"
                  << "Unflagged.esl\n";
    }

    for (const auto& plugin : {
             std::pair{"Flagged.esp", static_cast<uint32_t>(Record::FLAGS::kESL)}, std::pair{"Standard.esp", uint32_t{0}},
             std::pair{"Flagged.esm", static_cast<uint32_t>(Record::FLAGS::kESL)}, std::pair{"Standard.esm", uint32_t{0}},
             std::pair{"Flagged.esl", static_cast<uint32_t>(Record::FLAGS::kESL)}, std::pair{"Unflagged.esl", uint32_t{0}}})
    {
        std::ofstream pluginFile(dataDirectory.Path() / plugin.first, std::ios::binary);
        ASSERT_TRUE(pluginFile.good());
        Bytes data = MakePluginHeader(plugin.second);
        if (std::string(plugin.first) == "Flagged.esp")
            AppendRecord(data, FormEnum::NPC_, 0x00000001, {});
        pluginFile.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }

    ESLoader::ESLoader loader(dataDirectory.Path());
    const auto metadataOnly = loader.BuildRecordCollection(false);
    ASSERT_NE(metadataOnly, nullptr);
    EXPECT_FALSE(metadataOnly->HasAnyRecords());

    const auto& plugins = loader.GetLoadOrder();
    ASSERT_EQ(plugins.size(), 6U);

    EXPECT_EQ(plugins[0].m_filename, "Flagged.esp");
    EXPECT_TRUE(plugins[0].IsLite());
    EXPECT_EQ(plugins[0].m_liteId, 0U);

    EXPECT_EQ(plugins[1].m_filename, "Standard.esp");
    EXPECT_FALSE(plugins[1].IsLite());
    EXPECT_EQ(plugins[1].m_standardId, 0U);

    EXPECT_EQ(plugins[2].m_filename, "Flagged.esm");
    EXPECT_TRUE(plugins[2].IsLite());
    EXPECT_EQ(plugins[2].m_liteId, 1U);

    EXPECT_EQ(plugins[3].m_filename, "Standard.esm");
    EXPECT_FALSE(plugins[3].IsLite());
    EXPECT_EQ(plugins[3].m_standardId, 1U);

    EXPECT_EQ(plugins[4].m_filename, "Flagged.esl");
    EXPECT_TRUE(plugins[4].IsLite());
    EXPECT_EQ(plugins[4].m_liteId, 2U);

    EXPECT_EQ(plugins[5].m_filename, "Unflagged.esl");
    EXPECT_TRUE(plugins[5].IsLite());
    EXPECT_EQ(plugins[5].m_liteId, 3U);

    const auto loadedRecords = loader.BuildRecordCollection(true);
    ASSERT_NE(loadedRecords, nullptr);
    EXPECT_NE(loadedRecords->FindNpcById(0xFE000001), nullptr);
    EXPECT_EQ(loadedRecords->FindNpcById(0x00000001), nullptr);
}

TEST(ESLoader, RejectsPluginCountsThatExceedFormIdNamespaces)
{
    TemporaryDirectory dataDirectory;
    ASSERT_TRUE(dataDirectory.IsCreated()) << dataDirectory.Error().message();

    const auto writeLoadOrder = [&](const char* apExtension, const uint32_t aPluginCount) {
        std::ofstream loadOrder(dataDirectory.Path() / "loadorder.txt", std::ios::trunc);
        if (!loadOrder.good())
            return false;

        for (uint32_t i = 0; i < aPluginCount; ++i)
            loadOrder << "Plugin" << i << apExtension << '\n';

        return loadOrder.good();
    };

    ESLoader::ESLoader loader(dataDirectory.Path());

    const uint32_t standardPluginCapacity = ESLoader::kMaxStandardPluginId + 1;
    ASSERT_TRUE(writeLoadOrder(".esp", standardPluginCapacity));
    ASSERT_NE(loader.BuildRecordCollection(false), nullptr);
    ASSERT_EQ(loader.GetLoadOrder().size(), standardPluginCapacity);
    EXPECT_EQ(loader.GetLoadOrder().front().m_standardId, 0U);
    EXPECT_EQ(loader.GetLoadOrder().back().m_standardId, ESLoader::kMaxStandardPluginId);

    ASSERT_TRUE(writeLoadOrder(".esp", standardPluginCapacity + 1));
    EXPECT_EQ(loader.BuildRecordCollection(false), nullptr);
    EXPECT_TRUE(loader.GetLoadOrder().empty());

    const uint32_t litePluginCapacity = ESLoader::kMaxLitePluginId + 1;
    ASSERT_TRUE(writeLoadOrder(".esl", litePluginCapacity));
    ASSERT_NE(loader.BuildRecordCollection(false), nullptr);
    ASSERT_EQ(loader.GetLoadOrder().size(), litePluginCapacity);
    EXPECT_EQ(loader.GetLoadOrder().front().m_liteId, 0U);
    EXPECT_EQ(loader.GetLoadOrder().back().m_liteId, ESLoader::kMaxLitePluginId);

    ASSERT_TRUE(writeLoadOrder(".esl", litePluginCapacity + 1));
    EXPECT_EQ(loader.BuildRecordCollection(false), nullptr);
    EXPECT_TRUE(loader.GetLoadOrder().empty());
}

TEST(ESLoader, TESFileSetupRejectsOutOfRangeFormIdPrefixes)
{
    TemporaryDirectory dataDirectory;
    ASSERT_TRUE(dataDirectory.IsCreated()) << dataDirectory.Error().message();

    const auto pluginPath = dataDirectory.Path() / "Empty.esp";
    const Bytes pluginData = MakePluginHeader(0);
    {
        std::ofstream plugin(pluginPath, std::ios::binary);
        ASSERT_TRUE(plugin.good());
        plugin.write(reinterpret_cast<const char*>(pluginData.data()), static_cast<std::streamsize>(pluginData.size()));
        ASSERT_TRUE(plugin.good());
    }

    TiltedPhoques::Map<TiltedPhoques::String, uint32_t> masterFiles;
    ESLoader::TESFile standardFile(masterFiles);
    EXPECT_TRUE(standardFile.Setup(static_cast<uint8_t>(ESLoader::kMaxStandardPluginId)));
    EXPECT_FALSE(standardFile.Setup(uint8_t{0xFE}));
    ASSERT_TRUE(standardFile.LoadFile(pluginPath));
    ESLoader::RecordCollection records;
    EXPECT_FALSE(standardFile.IndexRecords(records));
    EXPECT_TRUE(standardFile.Setup(static_cast<uint8_t>(ESLoader::kMaxStandardPluginId)));
    EXPECT_TRUE(standardFile.IndexRecords(records));

    ESLoader::TESFile liteFile(masterFiles);
    EXPECT_TRUE(liteFile.Setup(ESLoader::kMaxLitePluginId));
    EXPECT_FALSE(liteFile.Setup(static_cast<uint16_t>(ESLoader::kMaxLitePluginId + 1)));
    ASSERT_TRUE(liteFile.LoadFile(pluginPath));
    EXPECT_FALSE(liteFile.IndexRecords(records));
    EXPECT_TRUE(liteFile.Setup(ESLoader::kMaxLitePluginId));
    EXPECT_TRUE(liteFile.IndexRecords(records));
}

TEST(ESLoader, SkipsPluginsWithMalformedTES4Headers)
{
    TemporaryDirectory dataDirectory;
    ASSERT_TRUE(dataDirectory.IsCreated()) << dataDirectory.Error().message();

    {
        std::ofstream loadOrder(dataDirectory.Path() / "loadorder.txt");
        ASSERT_TRUE(loadOrder.good());
        loadOrder << "Truncated.esl\n"
                  << "WrongType.esp\n"
                  << "Oversized.esm\n"
                  << "Directory.esp\n";
    }

    const auto writeFile = [&](const char* apFilename, const Bytes& aData) {
        std::ofstream file(dataDirectory.Path() / apFilename, std::ios::binary);
        if (!file.good())
            return false;
        file.write(reinterpret_cast<const char*>(aData.data()), static_cast<std::streamsize>(aData.size()));
        return file.good();
    };

    const Bytes truncated{0x54, 0x45, 0x53, 0x34};
    Bytes wrongType;
    AppendRecord(wrongType, static_cast<FormEnum>(0x12345678U), 0, {});
    Bytes oversized = MakePluginHeader(0);
    const uint32_t declaredSize = std::numeric_limits<uint32_t>::max();
    std::memcpy(oversized.data() + sizeof(uint32_t), &declaredSize, sizeof(declaredSize));

    ASSERT_TRUE(writeFile("Truncated.esl", truncated));
    ASSERT_TRUE(writeFile("WrongType.esp", wrongType));
    ASSERT_TRUE(writeFile("Oversized.esm", oversized));

    std::error_code directoryError;
    ASSERT_TRUE(std::filesystem::create_directory(dataDirectory.Path() / "Directory.esp", directoryError)) << directoryError.message();

    EXPECT_FALSE(ESLoader::TESFile::ReadHeaderFlags(dataDirectory.Path() / "Truncated.esl").has_value());
    EXPECT_FALSE(ESLoader::TESFile::ReadHeaderFlags(dataDirectory.Path() / "WrongType.esp").has_value());
    EXPECT_FALSE(ESLoader::TESFile::ReadHeaderFlags(dataDirectory.Path() / "Oversized.esm").has_value());

    ESLoader::ESLoader loader(dataDirectory.Path());
    const auto metadataOnly = loader.BuildRecordCollection(false);
    ASSERT_NE(metadataOnly, nullptr);
    EXPECT_FALSE(metadataOnly->HasAnyRecords());

    const auto& plugins = loader.GetLoadOrder();
    EXPECT_TRUE(plugins.empty());
}

TEST(ESLoader, ResolvesReferencesToLightMasters)
{
    TemporaryDirectory dataDirectory;
    ASSERT_TRUE(dataDirectory.IsCreated()) << dataDirectory.Error().message();

    {
        std::ofstream loadOrder(dataDirectory.Path() / "loadorder.txt");
        ASSERT_TRUE(loadOrder.good());
        loadOrder << "EarlierLight.esl\n"
                  << "LightMaster.esp\n"
                  << "NextLight.esl\n"
                  << "Dependent.esp\n";
    }

    const Bytes earlierLight = MakePluginHeader(0);
    {
        std::ofstream file(dataDirectory.Path() / "EarlierLight.esl", std::ios::binary);
        ASSERT_TRUE(file.good());
        file.write(reinterpret_cast<const char*>(earlierLight.data()), static_cast<std::streamsize>(earlierLight.size()));
    }

    Bytes lightMaster = MakePluginHeader(Record::FLAGS::kESL);
    constexpr uint32_t lightMasterRaceRawId = 0x00000010;
    constexpr uint32_t lightMasterNpcRaceRawId = 0x00000010;
    AppendRecord(lightMaster, FormEnum::RACE, lightMasterRaceRawId, MakeRaceData("LightMasterRace"));
    AppendRecord(lightMaster, FormEnum::NPC_, 0x00000020, MakeNpcData("LightMasterNpc", &lightMasterNpcRaceRawId));
    AppendRecord(lightMaster, FormEnum::NPC_, 0x00000FFF, MakeNpcData("MaxLightLocalNpc", &lightMasterNpcRaceRawId));
    AppendRecord(lightMaster, FormEnum::ACHR, 0x00000030, MakeActorReferenceData(0x00000020));
    // This malformed light local ID would overflow into NextLight's namespace
    // if all 24 low bits were added to the light master prefix.
    AppendRecord(lightMaster, FormEnum::RACE, 0x00001001, MakeRaceData("InvalidAliasedLightRace"));
    {
        std::ofstream file(dataDirectory.Path() / "LightMaster.esp", std::ios::binary);
        ASSERT_TRUE(file.good());
        file.write(reinterpret_cast<const char*>(lightMaster.data()), static_cast<std::streamsize>(lightMaster.size()));
    }

    Bytes nextLight = MakePluginHeaderWithMaster("LightMaster.esp");
    constexpr uint32_t nextLightNpcRaceRawId = 0x00000010;
    AppendRecord(nextLight, FormEnum::NPC_, 0x01000001, MakeNpcData("NextLightNpc", &nextLightNpcRaceRawId));
    AppendRecord(nextLight, FormEnum::ACHR, 0x01000031, MakeActorReferenceData(0x00001001));
    {
        std::ofstream file(dataDirectory.Path() / "NextLight.esl", std::ios::binary);
        ASSERT_TRUE(file.good());
        file.write(reinterpret_cast<const char*>(nextLight.data()), static_cast<std::streamsize>(nextLight.size()));
    }

    Bytes dependent = MakePluginHeaderWithMaster("LightMaster.esp");
    constexpr uint32_t dependentRaceRawId = 0x00000010;
    AppendRecord(dependent, FormEnum::NPC_, 0x01000021, MakeNpcData("DependentNpc", &dependentRaceRawId));
    AppendRecord(dependent, FormEnum::ACHR, 0x01000031, MakeActorReferenceData(0x00000020));
    {
        std::ofstream file(dataDirectory.Path() / "Dependent.esp", std::ios::binary);
        ASSERT_TRUE(file.good());
        file.write(reinterpret_cast<const char*>(dependent.data()), static_cast<std::streamsize>(dependent.size()));
    }

    ESLoader::ESLoader loader(dataDirectory.Path());
    const auto records = loader.BuildRecordCollection(true);
    ASSERT_NE(records, nullptr);
    const auto& plugins = loader.GetLoadOrder();
    ASSERT_EQ(plugins.size(), 4U);
    EXPECT_TRUE(plugins[0].IsLite());
    EXPECT_TRUE(plugins[1].IsLite());
    EXPECT_EQ(plugins[1].m_liteId, 1U);
    EXPECT_TRUE(plugins[2].IsLite());
    EXPECT_EQ(plugins[2].m_liteId, 2U);
    EXPECT_FALSE(plugins[3].IsLite());
    const auto* const pLightMasterRace = records->FindRaceById(0xFE001010);
    ASSERT_NE(pLightMasterRace, nullptr);
    EXPECT_EQ(pLightMasterRace->m_editorId, "LightMasterRace");

    const auto* const pLightMasterNpc = records->FindNpcById(0xFE001020);
    ASSERT_NE(pLightMasterNpc, nullptr);
    EXPECT_EQ(pLightMasterNpc->m_raceId, 0xFE001010);

    const auto* const pMaxLightLocalNpc = records->FindNpcById(0xFE001FFF);
    ASSERT_NE(pMaxLightLocalNpc, nullptr);
    EXPECT_EQ(pMaxLightLocalNpc->m_editorId, "MaxLightLocalNpc");

    const auto* const pLightMasterActorReference = records->FindActorReferenceById(0xFE001030);
    ASSERT_NE(pLightMasterActorReference, nullptr);
    EXPECT_EQ(pLightMasterActorReference->m_baseObject.m_baseId, 0xFE001020);

    const auto* const pDependentNpc = records->FindNpcById(0x00000021);
    ASSERT_NE(pDependentNpc, nullptr);
    EXPECT_EQ(pDependentNpc->m_raceId, 0xFE001010);

    const auto* const pDependentActorReference = records->FindActorReferenceById(0x00000031);
    ASSERT_NE(pDependentActorReference, nullptr);
    EXPECT_EQ(pDependentActorReference->m_baseObject.m_baseId, 0xFE001020);

    const auto* const pNextLightNpc = records->FindNpcById(0xFE002001);
    ASSERT_NE(pNextLightNpc, nullptr);
    EXPECT_EQ(pNextLightNpc->m_editorId, "NextLightNpc");
    EXPECT_EQ(records->FindRaceById(0xFE002001), nullptr);

    const auto* const pNextLightActorReference = records->FindActorReferenceById(0xFE002031);
    ASSERT_NE(pNextLightActorReference, nullptr);
    EXPECT_EQ(pNextLightActorReference->m_baseObject.m_baseId, 0u);
    ActorPopulationPolicy policy(records.get());
    EXPECT_EQ(policy.ClassifyNpcBase(pNextLightActorReference->m_baseObject.m_baseId).Class, ActorPopulationClass::kUnknown);
    EXPECT_TRUE(records->HasAnyRecords());
}

TEST(ESLoader, RejectsMasterListsWithoutDistinctSelfParentSlot)
{
    TemporaryDirectory dataDirectory;
    ASSERT_TRUE(dataDirectory.IsCreated()) << dataDirectory.Error().message();

    TiltedPhoques::Map<String, uint32_t> masterPrefixes;
    std::vector<String> masterNames;
    masterNames.reserve(256);
    for (uint16_t i = 0; i < 256; ++i)
    {
        String masterName = "Master" + std::to_string(i) + ".esm";
        masterPrefixes.emplace(masterName, static_cast<uint32_t>(i) << 24);
        masterNames.push_back(std::move(masterName));
    }

    const auto makePluginWithMasters = [&masterNames](const size_t aMasterCount, const uint32_t aRaceFormId) {
        Bytes headerData;
        for (size_t i = 0; i < aMasterCount; ++i)
        {
            const String& masterName = masterNames[i];
            Bytes masterNameData(masterName.begin(), masterName.end());
            masterNameData.push_back(0);
            AppendChunk(headerData, ChunkId::MAST_ID, masterNameData);
            AppendChunk(headerData, ChunkId::DATA_ID, Bytes(sizeof(uint64_t), 0));
        }

        Bytes pluginData;
        AppendRecord(pluginData, FormEnum::TES4, 0, headerData);
        AppendRecord(pluginData, FormEnum::RACE, aRaceFormId, MakeRaceData("ParentSlotBoundaryRace"));
        return pluginData;
    };

    const auto writePlugin = [&dataDirectory](const char* apFilename, const Bytes& acPluginData) {
        std::ofstream plugin(dataDirectory.Path() / apFilename, std::ios::binary);
        if (!plugin.good())
            return false;
        plugin.write(reinterpret_cast<const char*>(acPluginData.data()), static_cast<std::streamsize>(acPluginData.size()));
        return plugin.good();
    };

    ASSERT_TRUE(writePlugin("MaxMasters.esp", makePluginWithMasters(255, 0xFF000001)));
    ASSERT_TRUE(writePlugin("TooManyMasters.esp", makePluginWithMasters(256, 0x00000001)));

    ESLoader::TESFile maxMastersFile(masterPrefixes);
    ASSERT_TRUE(maxMastersFile.Setup(1));
    ASSERT_TRUE(maxMastersFile.LoadFile(dataDirectory.Path() / "MaxMasters.esp"));
    ESLoader::RecordCollection maxMastersRecords;
    EXPECT_TRUE(maxMastersFile.IndexRecords(maxMastersRecords));
    const auto* const pBoundaryRace = maxMastersRecords.FindRaceById(0x01000001);
    ASSERT_NE(pBoundaryRace, nullptr);
    EXPECT_EQ(pBoundaryRace->m_editorId, "ParentSlotBoundaryRace");

    ESLoader::TESFile tooManyMastersFile(masterPrefixes);
    ASSERT_TRUE(tooManyMastersFile.Setup(1));
    ASSERT_TRUE(tooManyMastersFile.LoadFile(dataDirectory.Path() / "TooManyMasters.esp"));
    ESLoader::RecordCollection tooManyMastersRecords;
    EXPECT_FALSE(tooManyMastersFile.IndexRecords(tooManyMastersRecords));
    EXPECT_FALSE(tooManyMastersRecords.HasAnyRecords());
}

TEST(ESLoader, ResolvesActorPopulationRecordsAcrossMultipleMastersAndOverrides)
{
    TemporaryDirectory dataDirectory;
    ASSERT_TRUE(dataDirectory.IsCreated()) << dataDirectory.Error().message();

    {
        std::ofstream loadOrder(dataDirectory.Path() / "loadorder.txt");
        ASSERT_TRUE(loadOrder.good());
        loadOrder << "PriorPlugin.esm\n"
                  << "MasterA.esm\n"
                  << "MasterB.esp\n"
                  << "Dependent.esp\n"
                  << "Override.esp\n";
    }

    const auto writePlugin = [&](const char* apFilename, const Bytes& acPluginData) {
        std::ofstream plugin(dataDirectory.Path() / apFilename, std::ios::binary);
        if (!plugin.good())
            return false;
        plugin.write(reinterpret_cast<const char*>(acPluginData.data()), static_cast<std::streamsize>(acPluginData.size()));
        return plugin.good();
    };

    constexpr uint32_t masterARaceId = 0x00001000;
    constexpr uint32_t masterARawNpcRaceId = 0x00001000;
    constexpr uint32_t masterBRawRaceId = 0x00001000;
    Bytes priorPlugin = MakePluginHeaderWithMasters({});
    AppendRecord(priorPlugin, FormEnum::RACE, 0x00001000, MakeRaceData("NordRace"));
    AppendRecord(priorPlugin, FormEnum::NPC_, 0x00002000, MakeNpcData("PriorNordNpc", &masterARaceId));
    AppendRecord(priorPlugin, FormEnum::NPC_, 0, MakeNpcData("NullFormNordNpc", &masterARaceId));
    AppendRecord(priorPlugin, FormEnum::ACHR, 0x00009004, MakeActorReferenceData(0x00002000));
    Bytes masterA = MakePluginHeaderWithMasters({});
    AppendRecord(masterA, FormEnum::RACE, masterARaceId, MakeRaceData("MasterARace"));
    AppendRecord(masterA, FormEnum::NPC_, 0x00002000, MakeNpcData("MasterANpc", &masterARawNpcRaceId));
    AppendRecord(masterA, FormEnum::ACHR, 0x00003000, MakeActorReferenceData(0x00002000));

    Bytes masterB = MakePluginHeaderWithMasters({});
    AppendRecord(masterB, FormEnum::RACE, masterBRawRaceId, MakeRaceData("MasterBRace"));
    AppendRecord(masterB, FormEnum::NPC_, 0x00002000, MakeNpcData("MasterBNpc", &masterBRawRaceId));
    AppendRecord(masterB, FormEnum::ACHR, 0x00003000, MakeActorReferenceData(0x00002000));

    Bytes dependent = MakePluginHeaderWithMasters({"MasterA.esm", "MasterB.esp"});
    constexpr uint32_t dependentRaceRawId = 0x01001000;
    AppendRecord(dependent, FormEnum::NPC_, 0x02008000, MakeNpcData("DependentNpc", &dependentRaceRawId));
    constexpr uint32_t dependentMasterARaceRawId = 0x00001000;
    AppendRecord(dependent, FormEnum::NPC_, 0x02008001, MakeNpcData("DependentMasterANpc", &dependentMasterARaceRawId));
    AppendRecord(dependent, FormEnum::ACHR, 0x02009000, MakeActorReferenceData(0x01002000));
    AppendRecord(dependent, FormEnum::ACHR, 0x02009001, MakeActorReferenceData(0x00002000));

    Bytes overridePlugin = MakePluginHeaderWithMasters({"MasterA.esm", "MasterB.esp", "Dependent.esp"});
    AppendRecord(overridePlugin, FormEnum::NPC_, 0x00002000, MakeNpcData("OverriddenMasterANpc", &dependentRaceRawId));
    AppendRecord(overridePlugin, FormEnum::RACE, 0x01001000, MakeRaceData("OverriddenMasterBRace"));
    AppendRecord(overridePlugin, FormEnum::ACHR, 0x00003000, MakeActorReferenceData(0x01002000));
    AppendRecord(overridePlugin, FormEnum::ACHR, 0x03009002, MakeActorReferenceData(0x02008000));
    constexpr uint32_t unresolvedRaceRawId = 0x7F001000;
    constexpr uint32_t unresolvedNpcRawId = 0x7F002000;
    AppendRecord(overridePlugin, FormEnum::NPC_, 0x03008003, MakeNpcData("UnresolvedRaceNpc", &unresolvedRaceRawId));
    AppendRecord(overridePlugin, FormEnum::ACHR, 0x03009003, MakeActorReferenceData(unresolvedNpcRawId));
    AppendRecord(overridePlugin, FormEnum::NPC_, unresolvedNpcRawId, MakeNpcData("UnresolvedPrefixNpc", &masterARaceId));
    AppendRecord(overridePlugin, FormEnum::ACHR, 0x7F009004, MakeActorReferenceData(0x7F003003));
    AppendRecord(overridePlugin, FormEnum::RACE, unresolvedRaceRawId, MakeRaceData("UnresolvedPrefixRace"));

    ASSERT_TRUE(writePlugin("PriorPlugin.esm", priorPlugin));
    ASSERT_TRUE(writePlugin("MasterA.esm", masterA));
    ASSERT_TRUE(writePlugin("MasterB.esp", masterB));
    ASSERT_TRUE(writePlugin("Dependent.esp", dependent));
    ASSERT_TRUE(writePlugin("Override.esp", overridePlugin));

    ESLoader::ESLoader loader(dataDirectory.Path());
    const auto records = loader.BuildRecordCollection(true);
    ASSERT_NE(records, nullptr);

    const auto* const pMasterARace = records->FindRaceById(0x01001000);
    ASSERT_NE(pMasterARace, nullptr);
    EXPECT_EQ(pMasterARace->m_editorId, "MasterARace");

    const auto* const pMasterBRace = records->FindRaceById(0x02001000);
    ASSERT_NE(pMasterBRace, nullptr);
    EXPECT_EQ(pMasterBRace->m_editorId, "OverriddenMasterBRace");

    const auto* const pOverriddenNpc = records->FindNpcById(0x01002000);
    ASSERT_NE(pOverriddenNpc, nullptr);
    EXPECT_EQ(pOverriddenNpc->m_editorId, "OverriddenMasterANpc");
    EXPECT_EQ(pOverriddenNpc->m_raceId, 0x02001000);

    const auto* const pMasterBNpc = records->FindNpcById(0x02002000);
    ASSERT_NE(pMasterBNpc, nullptr);
    EXPECT_EQ(pMasterBNpc->m_raceId, 0x02001000);

    const auto* const pDependentNpc = records->FindNpcById(0x03008000);
    ASSERT_NE(pDependentNpc, nullptr);
    EXPECT_EQ(pDependentNpc->m_raceId, 0x02001000);

    const auto* const pDependentMasterANpc = records->FindNpcById(0x03008001);
    ASSERT_NE(pDependentMasterANpc, nullptr);
    EXPECT_EQ(pDependentMasterANpc->m_raceId, 0x01001000);

    const auto* const pOverriddenActorReference = records->FindActorReferenceById(0x01003000);
    ASSERT_NE(pOverriddenActorReference, nullptr);
    EXPECT_EQ(pOverriddenActorReference->m_baseObject.m_baseId, 0x02002000);

    const auto* const pMasterBActorReference = records->FindActorReferenceById(0x02003000);
    ASSERT_NE(pMasterBActorReference, nullptr);
    EXPECT_EQ(pMasterBActorReference->m_baseObject.m_baseId, 0x02002000);

    const auto* const pDependentActorReference = records->FindActorReferenceById(0x03009000);
    ASSERT_NE(pDependentActorReference, nullptr);
    EXPECT_EQ(pDependentActorReference->m_baseObject.m_baseId, 0x02002000);

    const auto* const pDependentMasterAActorReference = records->FindActorReferenceById(0x03009001);
    ASSERT_NE(pDependentMasterAActorReference, nullptr);
    EXPECT_EQ(pDependentMasterAActorReference->m_baseObject.m_baseId, 0x01002000);

    const auto* const pOverrideActorReferenceToDependentMaster = records->FindActorReferenceById(0x04009002);
    ASSERT_NE(pOverrideActorReferenceToDependentMaster, nullptr);
    EXPECT_EQ(pOverrideActorReferenceToDependentMaster->m_baseObject.m_baseId, 0x03008000);

    const auto* const pPriorNordRace = records->FindRaceById(0x00001000);
    ASSERT_NE(pPriorNordRace, nullptr);
    EXPECT_EQ(pPriorNordRace->m_editorId, "NordRace");

    const auto* const pPriorNordNpc = records->FindNpcById(0x00002000);
    ASSERT_NE(pPriorNordNpc, nullptr);
    EXPECT_EQ(pPriorNordNpc->m_editorId, "PriorNordNpc");

    const auto* const pPriorActorReference = records->FindActorReferenceById(0x00009004);
    ASSERT_NE(pPriorActorReference, nullptr);
    EXPECT_EQ(pPriorActorReference->m_baseObject.m_baseId, 0x00002000u);

    const auto* const pUnresolvedRaceNpc = records->FindNpcById(0x04008003);
    ASSERT_NE(pUnresolvedRaceNpc, nullptr);
    EXPECT_EQ(pUnresolvedRaceNpc->m_raceId, 0u);
    ActorPopulationPolicy policy(records.get());
    EXPECT_EQ(policy.ClassifyNpcBase(0x04008003).Class, ActorPopulationClass::kUnknown);
    EXPECT_EQ(policy.ClassifyNpcBase(0).Class, ActorPopulationClass::kUnknown);

    const auto* const pUnresolvedBaseActorReference = records->FindActorReferenceById(0x04009003);
    ASSERT_NE(pUnresolvedBaseActorReference, nullptr);
    EXPECT_EQ(pUnresolvedBaseActorReference->m_baseObject.m_baseId, 0u);

    ModsComponent mods;
    ESLoader::PluginData overridePluginData{};
    overridePluginData.m_filename = "Override.esp";
    overridePluginData.m_standardId = 4;
    mods.AddServerMod(overridePluginData);
    const uint32_t networkModId = mods.AddStandard("Override.esp");
    ActorPopulationIdentityResolver resolver(mods, records.get(), policy);
    const auto unresolvedActor = resolver.Resolve(GameId(networkModId, 0x009003));
    EXPECT_EQ(unresolvedActor.Source, ActorPopulationIdentitySource::kServerPlacedReference);
    EXPECT_EQ(unresolvedActor.Classification.Class, ActorPopulationClass::kUnknown);
}

TEST(ESLoader, MissingLoadOrderClearsPreviouslyLoadedMetadata)
{
    TemporaryDirectory dataDirectory;
    ASSERT_TRUE(dataDirectory.IsCreated()) << dataDirectory.Error().message();

    {
        std::ofstream loadOrder(dataDirectory.Path() / "loadorder.txt");
        ASSERT_TRUE(loadOrder.good());
        loadOrder << "Skyrim.esm\nTest.esp\n";
    }

    ESLoader::ESLoader loader(dataDirectory.Path());
    ASSERT_NE(loader.BuildRecordCollection(false), nullptr);
    ASSERT_EQ(loader.GetLoadOrder().size(), 2U);

    std::error_code error;
    ASSERT_TRUE(std::filesystem::remove(dataDirectory.Path() / "loadorder.txt", error));
    ASSERT_FALSE(error);
    EXPECT_EQ(loader.BuildRecordCollection(false), nullptr);
    EXPECT_TRUE(loader.GetLoadOrder().empty());
}

void AddServerPlugin(ModsComponent& aMods, const char* apFilename, const uint16_t aLoadOrderId, const bool aIsLite)
{
    ESLoader::PluginData plugin{};
    plugin.m_filename = apFilename;
    plugin.m_isLite = aIsLite;
    if (aIsLite)
        plugin.m_liteId = aLoadOrderId;
    else
        plugin.m_standardId = static_cast<uint8_t>(aLoadOrderId);

    aMods.AddServerMod(plugin);
}

TEST(ActorPopulationIdentityResolver, ResolvesStandardAndLightServerFormIds)
{
    ModsComponent mods;
    AddServerPlugin(mods, "Test.esp", 2, false);
    AddServerPlugin(mods, "Light.esp", 7, true);
    AddServerPlugin(mods, "LastStandard.esp", ESLoader::kMaxStandardPluginId, false);
    AddServerPlugin(mods, "LastLight.esl", ESLoader::kMaxLitePluginId, true);

    const auto standardNetworkId = mods.AddStandard("Test.esp");
    const auto lightNetworkId = mods.AddLite("Light.esp");
    const auto lastStandardNetworkId = mods.AddStandard("LastStandard.esp");
    const auto lastLightNetworkId = mods.AddLite("LastLight.esl");
    const auto mismatchedNetworkId = mods.AddStandard("Light.esp");
    const auto unknownNetworkId = mods.AddStandard("Unknown.esp");

    uint32_t resolvedFormId = 0;
    EXPECT_TRUE(mods.ResolveServerFormId(GameId(standardNetworkId, 0xAB123456), resolvedFormId));
    EXPECT_EQ(resolvedFormId, 0x02123456u);

    EXPECT_TRUE(mods.ResolveServerFormId(GameId(lightNetworkId, 0x12345ABC), resolvedFormId));
    EXPECT_EQ(resolvedFormId, 0xFE007ABCu);

    EXPECT_TRUE(mods.ResolveServerFormId(GameId(lastStandardNetworkId, 0xFFFFFFFF), resolvedFormId));
    EXPECT_EQ(resolvedFormId, 0xFDFFFFFFu);

    EXPECT_TRUE(mods.ResolveServerFormId(GameId(lastLightNetworkId, 0xFFFFFFFF), resolvedFormId));
    EXPECT_EQ(resolvedFormId, 0xFEFFFFFFu);

    EXPECT_FALSE(mods.ResolveServerFormId(GameId(mismatchedNetworkId, 0x00000ABC), resolvedFormId));
    EXPECT_FALSE(mods.ResolveServerFormId(GameId(unknownNetworkId, 0x00000ABC), resolvedFormId));
}

TEST(ModsComponent, RejectsOutOfRangeServerPluginLoadOrderIds)
{
    ModsComponent mods;
    AddServerPlugin(mods, "InvalidStandard.esp", 0xFE, false);
    AddServerPlugin(mods, "InvalidLight.esl", ESLoader::kMaxLitePluginId + 1, true);

    const auto standardNetworkId = mods.AddStandard("InvalidStandard.esp");
    const auto liteNetworkId = mods.AddLite("InvalidLight.esl");
    uint32_t resolvedFormId = 0;

    EXPECT_FALSE(mods.ResolveServerFormId(GameId(standardNetworkId, 0x00001234), resolvedFormId));
    EXPECT_FALSE(mods.ResolveServerFormId(GameId(liteNetworkId, 0x00000123), resolvedFormId));
}

TEST(ActorPopulationAssignmentPolicy, AppliesGateTrustAndUnknownRules)
{
    ActorPopulationIdentity humanoid;
    humanoid.Source = ActorPopulationIdentitySource::kServerPlacedReference;
    humanoid.Classification.Class = ActorPopulationClass::kHumanoidNpc;

    ActorPopulationIdentity creature;
    creature.Source = ActorPopulationIdentitySource::kServerPlacedReference;
    creature.Classification.Class = ActorPopulationClass::kCreature;

    ActorPopulationIdentity unknown;
    unknown.Source = ActorPopulationIdentitySource::kUnknown;

    ActorPopulationIdentity clientCreatureClaim;
    clientCreatureClaim.Source = ActorPopulationIdentitySource::kClientClaimedTemporaryBase;
    clientCreatureClaim.ClientClaimedClassification.Class = ActorPopulationClass::kCreature;

    ActorPopulationIdentity conflictingClaim = humanoid;
    conflictingClaim.HasClientClaimedIdentity = true;
    conflictingClaim.ClientClaimedClassification.Class = ActorPopulationClass::kCreature;

    ActorPopulationIdentity player;
    player.IsPlayer = true;
    player.Source = ActorPopulationIdentitySource::kPlayer;
    player.Classification.Class = ActorPopulationClass::kPlayer;

    ActorPopulationAssignmentPolicy disabled(false, false);
    EXPECT_EQ(disabled.Decide(humanoid), ActorPopulationAssignmentDecision::kAllow);

    ActorPopulationAssignmentPolicy permissive(true, true);
    EXPECT_EQ(permissive.Decide(humanoid), ActorPopulationAssignmentDecision::kRejectHumanoid);
    EXPECT_EQ(permissive.Decide(creature), ActorPopulationAssignmentDecision::kAllow);
    EXPECT_EQ(permissive.Decide(unknown), ActorPopulationAssignmentDecision::kAllow);
    EXPECT_EQ(permissive.Decide(clientCreatureClaim), ActorPopulationAssignmentDecision::kAllow);
    EXPECT_EQ(permissive.Decide(conflictingClaim), ActorPopulationAssignmentDecision::kRejectHumanoid);
    EXPECT_EQ(permissive.Decide(player), ActorPopulationAssignmentDecision::kAllow);

    ActorPopulationAssignmentPolicy strict(true, false);
    EXPECT_EQ(strict.Decide(unknown), ActorPopulationAssignmentDecision::kRejectUnknown);
    EXPECT_EQ(strict.Decide(clientCreatureClaim), ActorPopulationAssignmentDecision::kRejectUnknown);
    EXPECT_EQ(strict.Decide(conflictingClaim), ActorPopulationAssignmentDecision::kRejectHumanoid);
}

TEST_F(ActorPopulationTests, ResolvesPlacedActorsWithServerAuthorityAndKeepsClaimsUntrusted)
{
    ModsComponent mods;
    AddServerPlugin(mods, "Test.esp", 1, false);
    const auto networkModId = mods.AddStandard("Test.esp");

    ActorPopulationPolicy policy(&m_records);
    policy.SetRaceClassification("NordRace", ActorPopulationClass::kHumanoidNpc);
    policy.SetRaceClassification("WolfRace", ActorPopulationClass::kCreature);

    ActorPopulationIdentityResolver resolver(mods, &m_records, policy);

    const auto player = resolver.Resolve(GameId(0, 0x14), GameId(networkModId, kWolfNpcId & 0x00FFFFFFu));
    EXPECT_TRUE(player.IsPlayer);
    EXPECT_TRUE(player.IsTrusted());
    EXPECT_EQ(player.Source, ActorPopulationIdentitySource::kPlayer);
    EXPECT_EQ(player.Classification.Class, ActorPopulationClass::kPlayer);

    const auto humanoid = resolver.Resolve(GameId(networkModId, kNordActorReferenceId & 0x00FFFFFFu));
    EXPECT_TRUE(humanoid.IsTrusted());
    EXPECT_EQ(humanoid.Source, ActorPopulationIdentitySource::kServerPlacedReference);
    EXPECT_EQ(humanoid.ResolvedReferenceFormId, kNordActorReferenceId);
    EXPECT_EQ(humanoid.ResolvedNpcFormId, kNordNpcId);
    EXPECT_EQ(humanoid.Classification.Class, ActorPopulationClass::kHumanoidNpc);

    const auto creature = resolver.Resolve(GameId(networkModId, kWolfActorReferenceId & 0x00FFFFFFu));
    EXPECT_TRUE(creature.IsTrusted());
    EXPECT_EQ(creature.Classification.Class, ActorPopulationClass::kCreature);

    const auto conflictingClaim = resolver.Resolve(
        GameId(networkModId, kNordActorReferenceId & 0x00FFFFFFu), GameId(networkModId, kWolfNpcId & 0x00FFFFFFu));
    EXPECT_EQ(conflictingClaim.Source, ActorPopulationIdentitySource::kServerPlacedReference);
    EXPECT_TRUE(conflictingClaim.IsTrusted());
    EXPECT_EQ(conflictingClaim.Classification.Class, ActorPopulationClass::kHumanoidNpc);
    EXPECT_TRUE(conflictingClaim.HasClientClaimedIdentity);
    EXPECT_EQ(conflictingClaim.ClientClaimedNpcFormId, kWolfNpcId);
    EXPECT_EQ(conflictingClaim.ClientClaimedClassification.Class, ActorPopulationClass::kCreature);

    const auto missingReference = resolver.Resolve(
        GameId(networkModId, 0x00DEAD00u), GameId(networkModId, kWolfNpcId & 0x00FFFFFFu));
    EXPECT_EQ(missingReference.Source, ActorPopulationIdentitySource::kUnknown);
    EXPECT_FALSE(missingReference.IsTrusted());
    EXPECT_EQ(missingReference.Classification.Class, ActorPopulationClass::kUnknown);
    EXPECT_TRUE(missingReference.HasClientClaimedIdentity);
    EXPECT_EQ(missingReference.ClientClaimedClassification.Class, ActorPopulationClass::kCreature);

    const auto nonNpcBase = resolver.Resolve(GameId(networkModId, kMissingNpcActorReferenceId & 0x00FFFFFFu));
    EXPECT_EQ(nonNpcBase.Source, ActorPopulationIdentitySource::kServerPlacedReference);
    EXPECT_EQ(nonNpcBase.Classification.Class, ActorPopulationClass::kUnknown);

    const auto temporaryClaim = resolver.Resolve(
        GameId(std::numeric_limits<uint32_t>::max(), 0x00000042u), GameId(networkModId, kWolfNpcId & 0x00FFFFFFu));
    EXPECT_EQ(temporaryClaim.Source, ActorPopulationIdentitySource::kClientClaimedTemporaryBase);
    EXPECT_FALSE(temporaryClaim.IsTrusted());
    EXPECT_EQ(temporaryClaim.Classification.Class, ActorPopulationClass::kUnknown);
    EXPECT_EQ(temporaryClaim.ClientClaimedClassification.Class, ActorPopulationClass::kCreature);

    const auto leveledPickClaim = resolver.Resolve(
        GameId(networkModId, 0x00DEAD00u), GameId{}, GameId(networkModId, kWolfNpcId & 0x00FFFFFFu));
    EXPECT_EQ(leveledPickClaim.Source, ActorPopulationIdentitySource::kClientClaimedLeveledPick);
    EXPECT_FALSE(leveledPickClaim.IsTrusted());
    EXPECT_EQ(leveledPickClaim.Classification.Class, ActorPopulationClass::kUnknown);
    EXPECT_EQ(leveledPickClaim.ClientClaimedClassification.Class, ActorPopulationClass::kCreature);

    ESLoader::RecordCollection emptyRecords;
    ActorPopulationIdentityResolver emptyResolver(mods, &emptyRecords, policy);
    const auto withoutRecords = emptyResolver.Resolve(GameId(networkModId, kNordActorReferenceId & 0x00FFFFFFu));
    EXPECT_EQ(withoutRecords.Source, ActorPopulationIdentitySource::kUnknown);
    EXPECT_EQ(withoutRecords.Classification.Class, ActorPopulationClass::kUnknown);
}

TEST_F(ActorPopulationTests, KeepsRequiredUnknownConditionsDistinct)
{
    ActorPopulationPolicy policy(&m_records);

    EXPECT_EQ(policy.ClassifyNpcBase(0xDEADBEEF).Class, ActorPopulationClass::kUnknown);
    ASSERT_NE(m_records.FindNpcById(kMissingRnamNpcId), nullptr);
    EXPECT_EQ(m_records.FindNpcById(kMissingRnamNpcId)->m_raceId, 0u);
    EXPECT_EQ(policy.ClassifyNpcBase(kMissingRnamNpcId).Class, ActorPopulationClass::kUnknown);
    EXPECT_EQ(policy.ClassifyNpcBase(kMissingRaceNpcId).Class, ActorPopulationClass::kUnknown);
    EXPECT_EQ(policy.ClassifyNpcBase(kNoEditorNpcId).Class, ActorPopulationClass::kUnknown);
    EXPECT_EQ(policy.ClassifyNpcBase(kWolfNpcId).Class, ActorPopulationClass::kUnknown);

    EXPECT_EQ(policy.ClassifyActor(GameId(0x42, kNordNpcId)).Class, ActorPopulationClass::kUnknown);
}
} // namespace
