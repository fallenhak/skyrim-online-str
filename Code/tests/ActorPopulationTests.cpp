#include <Services/ActorPopulationPolicy.h>

#include <RecordCollection.h>
#include <Records/TESFileRecordTypes.inl>
#include <TESFile.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace
{
using Bytes = std::vector<uint8_t>;

constexpr uint32_t kMasterPrefix = 0x02000000;
constexpr uint32_t kNordRaceRawId = 0x01001000;
constexpr uint32_t kWolfRaceRawId = 0x01001100;
constexpr uint32_t kDraugrRaceRawId = 0x01001200;
constexpr uint32_t kEdgeRaceRawId = 0x01001300;
constexpr uint32_t kNoEditorRaceRawId = 0x01001400;

constexpr uint32_t kNordNpcRawId = 0x01002000;
constexpr uint32_t kWolfNpcRawId = 0x01002100;
constexpr uint32_t kDraugrNpcRawId = 0x01002200;
constexpr uint32_t kEdgeNpcRawId = 0x01002300;
constexpr uint32_t kMissingRnamNpcRawId = 0x01002400;
constexpr uint32_t kMissingRaceNpcRawId = 0x01002500;
constexpr uint32_t kNoEditorNpcRawId = 0x01002600;
constexpr uint32_t kMasterNpcRawId = 0x01002700;

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

void AppendRecord(Bytes& aBytes, FormEnum aFormType, uint32_t aFormId, const Bytes& aData)
{
    AppendValue(aBytes, static_cast<uint32_t>(aFormType));
    AppendValue(aBytes, static_cast<uint32_t>(aData.size()));
    AppendValue(aBytes, uint32_t{}); // flags
    AppendValue(aBytes, aFormId);
    AppendValue(aBytes, uint32_t{}); // version control info
    AppendValue(aBytes, uint16_t{}); // form version
    AppendValue(aBytes, uint16_t{}); // version control version
    aBytes.insert(aBytes.end(), aData.begin(), aData.end());
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

    AppendRecord(data, FormEnum::NPC_, kNordNpcRawId, MakeNpcData("NordNpc", &kNordRaceRawId));
    AppendRecord(data, FormEnum::NPC_, kWolfNpcRawId, MakeNpcData("WolfNpc", &kWolfRaceRawId));
    AppendRecord(data, FormEnum::NPC_, kDraugrNpcRawId, MakeNpcData("DraugrNpc", &kDraugrRaceRawId));
    AppendRecord(data, FormEnum::NPC_, kEdgeNpcRawId, MakeNpcData("EdgeNpc", &kEdgeRaceRawId));
    AppendRecord(data, FormEnum::NPC_, kMissingRnamNpcRawId, MakeNpcData("MissingRnamNpc", nullptr));

    constexpr uint32_t missingRaceRawId = 0x00001FFF;
    AppendRecord(data, FormEnum::NPC_, kMissingRaceNpcRawId, MakeNpcData("MissingRaceNpc", &missingRaceRawId));
    AppendRecord(data, FormEnum::NPC_, kNoEditorNpcRawId, MakeNpcData("NoEditorNpc", &kNoEditorRaceRawId));
    AppendRecord(data, FormEnum::NPC_, kMasterNpcRawId, MakeNpcData("MasterRefNpc", &kMasterRaceRawId));

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

        TiltedPhoques::Map<TiltedPhoques::String, uint8_t> masterFiles;
        masterFiles.emplace("Master.esm", uint8_t{2});
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

    const auto& constRecords = m_records;
    EXPECT_EQ(constRecords.FindNpcById(0xDEADBEEF), nullptr);
    EXPECT_EQ(constRecords.FindRaceById(0xDEADBEEF), nullptr);
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

TEST(ActorPopulationPolicy, KeepsNpcUnknownWithoutLoadedRecords)
{
    ESLoader::RecordCollection records;
    ActorPopulationPolicy policy(&records);

    EXPECT_EQ(policy.ClassifyNpcBase(kNordNpcId).Class, ActorPopulationClass::kUnknown);
    EXPECT_EQ(policy.ClassifyActor(GameId(0, 0x14)).Class, ActorPopulationClass::kPlayer);
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
    EXPECT_EQ(policy.ClassifyNpcBase(kNordNpcId).Class, ActorPopulationClass::kUnknown);

    EXPECT_EQ(policy.ClassifyActor(GameId(0x42, kNordNpcId)).Class, ActorPopulationClass::kUnknown);
}
} // namespace
