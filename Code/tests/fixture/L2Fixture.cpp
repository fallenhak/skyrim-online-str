#include "L2Fixture.h"

#include <cstring>
#include <fstream>
#include <string_view>
#include <system_error>

// Layout: https://en.uesp.net/wiki/Skyrim_Mod:Mod_File_Format
// Plain std only, so the bot and the tests can both link it without the ESLoader headers.
namespace L2Fixture
{
namespace
{
using Bytes = std::vector<uint8_t>;

constexpr uint32_t FourCC(const std::string_view aTag)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(aTag[0])) | static_cast<uint32_t>(static_cast<uint8_t>(aTag[1])) << 8 |
           static_cast<uint32_t>(static_cast<uint8_t>(aTag[2])) << 16 | static_cast<uint32_t>(static_cast<uint8_t>(aTag[3])) << 24;
}

// GRUP types
constexpr int32_t kTopGroup = 0;
constexpr int32_t kInteriorCellBlock = 2;
constexpr int32_t kInteriorCellSubBlock = 3;
constexpr int32_t kCellChildren = 6;
constexpr int32_t kCellPersistentChildren = 8;

constexpr uint32_t kMasterFileFlag = 0x1;

template <class T> void Append(Bytes& aBytes, const T aValue)
{
    uint8_t raw[sizeof(T)];
    std::memcpy(raw, &aValue, sizeof(T));
    aBytes.insert(aBytes.end(), raw, raw + sizeof(T));
}

void Append(Bytes& aBytes, const Bytes& aOther)
{
    aBytes.insert(aBytes.end(), aOther.begin(), aOther.end());
}

class Record
{
public:
    Record& Field(const std::string_view aTag, const Bytes& aPayload)
    {
        Append(m_data, FourCC(aTag));
        Append(m_data, static_cast<uint16_t>(aPayload.size()));
        Append(m_data, aPayload);
        return *this;
    }

    Record& ZString(const std::string_view aTag, const std::string_view aValue)
    {
        Bytes payload(aValue.begin(), aValue.end());
        payload.push_back(0);
        return Field(aTag, payload);
    }

    Record& FormId(const std::string_view aTag, const uint32_t aFormId)
    {
        Bytes payload;
        Append(payload, aFormId);
        return Field(aTag, payload);
    }

    Bytes Serialize(const std::string_view aType, const uint32_t aFormId, const uint32_t aFlags = 0) const
    {
        Bytes bytes;
        Append(bytes, FourCC(aType));
        Append(bytes, static_cast<uint32_t>(m_data.size()));
        Append(bytes, aFlags);
        Append(bytes, aFormId);
        Append(bytes, uint32_t{}); // version control
        Append(bytes, uint16_t{44}); // form version (Skyrim SE)
        Append(bytes, uint16_t{}); // version control version
        Append(bytes, m_data);
        return bytes;
    }

private:
    Bytes m_data;
};

Bytes Group(const uint32_t aLabel, const int32_t aGroupType, const Bytes& aContents)
{
    Bytes bytes;
    Append(bytes, FourCC("GRUP"));
    Append(bytes, static_cast<uint32_t>(24 + aContents.size()));
    Append(bytes, aLabel);
    Append(bytes, aGroupType);
    Append(bytes, uint16_t{}); // timestamp
    Append(bytes, uint16_t{}); // version control
    Append(bytes, uint32_t{});
    Append(bytes, aContents);
    return bytes;
}

Bytes TopGroup(const std::string_view aType, const Bytes& aRecord)
{
    return Group(FourCC(aType), kTopGroup, aRecord);
}

Bytes LeveledEntry(const uint16_t aLevel, const uint32_t aFormId, const uint16_t aCount)
{
    Bytes payload;
    Append(payload, aLevel);
    Append(payload, uint16_t{});
    Append(payload, aFormId);
    Append(payload, aCount);
    Append(payload, uint16_t{});
    return payload;
}

Bytes Header()
{
    Bytes hedr;
    Append(hedr, 1.71f); // version
    Append(hedr, uint32_t{17}); // number of records and groups (informational)
    Append(hedr, uint32_t{0x1000}); // next object id
    return Record{}.Field("HEDR", hedr).ZString("CNAM", "L2 bot").Serialize("TES4", 0, kMasterFileFlag);
}

Bytes EncounterZone()
{
    Bytes data;
    Append(data, uint32_t{}); // owner
    Append(data, uint32_t{}); // location
    Append(data, int8_t{});   // rank
    Append(data, static_cast<int8_t>(kZoneMinLevel));
    Append(data, uint8_t{}); // flags
    Append(data, static_cast<int8_t>(kZoneMaxLevel));
    return Record{}.ZString("EDID", "L2FixtureZone").Field("DATA", data).Serialize("ECZN", kEncounterZone);
}

Bytes LootList()
{
    return Record{}
        .ZString("EDID", "L2FixtureLoot")
        .Field("LVLD", {0})
        .Field("LVLF", {0x01}) // calculate from all levels
        .Field("LLCT", {2})
        .Field("LVLO", LeveledEntry(1, kGold, 5))
        .Field("LVLO", LeveledEntry(10, kKey, 1))
        .Serialize("LVLI", kLootList);
}

Bytes Chest()
{
    Bytes loot;
    Append(loot, kLootList);
    Append(loot, uint32_t{1});
    Bytes gold;
    Append(gold, kGold);
    Append(gold, kChestGold);
    Bytes count;
    Append(count, uint32_t{2});
    return Record{}
        .ZString("EDID", "L2FixtureChest")
        .Field("COCT", count)
        .Field("CNTO", loot)
        .Field("CNTO", gold)
        .Serialize("CONT", kChest);
}

Bytes Race()
{
    return Record{}.ZString("EDID", "L2FixtureRace").Serialize("RACE", kRace);
}

Bytes Bandit()
{
    Bytes acbs(24, 0);
    const uint16_t level = 1;
    std::memcpy(acbs.data() + 8, &level, sizeof(level));
    return Record{}.ZString("EDID", "L2FixtureBandit").Field("ACBS", acbs).FormId("RNAM", kRace).Serialize("NPC_", kBandit);
}

Bytes BanditList()
{
    return Record{}
        .ZString("EDID", "L2FixtureBanditList")
        .Field("LVLD", {0})
        .Field("LVLF", {0x01})
        .Field("LLCT", {1})
        .Field("LVLO", LeveledEntry(1, kBandit, 1))
        .Serialize("LVLN", kBanditList);
}

Bytes Door() { return Record{}.ZString("EDID", "L2FixtureDoor").Serialize("DOOR", kDoor); }
Bytes Lever() { return Record{}.ZString("EDID", "L2FixtureLever").Serialize("ACTI", kLever); }
Bytes DummyItem() { return Record{}.ZString("EDID", "L2FixtureDummy").Serialize("MISC", kDummyItem); }
Bytes Key() { return Record{}.ZString("EDID", "L2FixtureKey").Serialize("KEYM", kKey); }

Bytes Position(const float aX)
{
    Bytes data;
    for (const float value : {aX, 0.f, 0.f, 0.f, 0.f, 0.f})
        Append(data, value);
    return data;
}

Bytes Cell()
{
    Bytes data;
    Append(data, uint16_t{0x0001}); // interior
    const Bytes cell = Record{}.ZString("EDID", "L2FixtureCell").Field("DATA", data).FormId("XEZN", kEncounterZone).Serialize("CELL", kCell);

    Bytes lock;
    Append(lock, kChestLockLevel);
    Append(lock, uint8_t{});
    Append(lock, uint16_t{});
    Append(lock, kKey);
    Append(lock, uint32_t{}); // flags and padding
    Append(lock, uint64_t{}); // unknown

    Bytes references;
    Append(references, Record{}.FormId("NAME", kBanditList).Field("DATA", Position(0.f)).Serialize("ACHR", kBanditRef));
    Append(references, Record{}.FormId("NAME", kChest).Field("XLOC", lock).Field("DATA", Position(100.f)).Serialize("REFR", kChestRef));
    Append(references, Record{}.FormId("NAME", kDoor).Field("DATA", Position(200.f)).Serialize("REFR", kDoorRef));
    Append(references, Record{}.FormId("NAME", kLever).Field("DATA", Position(300.f)).Serialize("REFR", kLeverRef));
    Append(references, Record{}.FormId("NAME", kDummyItem).FormId("XLIB", kLootList).Field("DATA", Position(400.f)).Serialize("REFR", kPlacedLootRef));

    Bytes children = Group(kCell, kCellChildren, Group(kCell, kCellPersistentChildren, references));

    Bytes subBlockContents = cell;
    Append(subBlockContents, children);
    // Block and sub-block numbers come from the last digits of the cell's form id.
    const Bytes subBlock = Group(kCell % 10, kInteriorCellSubBlock, subBlockContents);
    return Group(FourCC("CELL"), kTopGroup, Group(kCell % 10, kInteriorCellBlock, subBlock));
}
} // namespace

std::vector<uint8_t> BuildPlugin()
{
    Bytes plugin = Header();
    Append(plugin, TopGroup("RACE", Race()));
    Append(plugin, TopGroup("DOOR", Door()));
    Append(plugin, TopGroup("MISC", DummyItem()));
    Append(plugin, TopGroup("KEYM", Key()));
    Append(plugin, TopGroup("CONT", Chest()));
    Append(plugin, TopGroup("NPC_", Bandit()));
    Append(plugin, TopGroup("ACTI", Lever()));
    Append(plugin, TopGroup("LVLN", BanditList()));
    Append(plugin, TopGroup("LVLI", LootList()));
    Append(plugin, TopGroup("ECZN", EncounterZone()));
    Append(plugin, Cell());
    return plugin;
}

bool WriteDataDirectory(const std::filesystem::path& aDataDirectory)
{
    std::error_code error;
    std::filesystem::create_directories(aDataDirectory, error);
    if (error)
        return false;

    const Bytes plugin = BuildPlugin();
    {
        std::ofstream file(aDataDirectory / kPluginName, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(plugin.data()), static_cast<std::streamsize>(plugin.size()));
        if (!file)
            return false;
    }

    std::ofstream loadOrder(aDataDirectory / "loadorder.txt", std::ios::trunc);
    loadOrder << kPluginName << '\n';
    return static_cast<bool>(loadOrder);
}
} // namespace L2Fixture
