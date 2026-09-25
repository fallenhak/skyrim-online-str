#pragma once

#include <filesystem>
#include <optional>

#include <RecordCollection.h>

#include <Records/CLMT.h>
#include <Records/GMST.h>
#include <Records/Group.h>
#include <Records/ACHR.h>
#include <Records/NPC.h>
#include <Records/RACE.h>
#include <Records/REFR.h>
#include <Records/TES4.h>

namespace ESLoader
{
// Skyrim reserves the final two standard load-order bytes for the light and
// runtime namespaces. Light plugins use twelve load-order bits in the FE
// namespace.
inline constexpr uint16_t kMaxStandardPluginId = 0xFD;
inline constexpr uint16_t kMaxLitePluginId = 0x0FFF;

class TESFile
{
public:
    TESFile() = default;
    TESFile(TiltedPhoques::Map<String, uint32_t>& aMasterFiles);

    bool Setup(uint8_t aStandardId);
    bool Setup(uint16_t aLiteId);
    [[nodiscard]] static std::optional<uint32_t> ReadHeaderFlags(const std::filesystem::path& acPath) noexcept;
    bool LoadFile(const std::filesystem::path& acPath) noexcept;
    bool IndexRecords(RecordCollection& aRecordCollection) noexcept;

    [[nodiscard]] static std::optional<uint32_t> GetFormIdPrefix(
        uint32_t aFormId, TiltedPhoques::Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept;

private:
    bool InitializeFormIdPrefixes() noexcept;
    bool ReadGroupOrRecord(Buffer::Reader& aReader, RecordCollection& aRecordCollection, size_t aParentEnd, size_t aGroupDepth) noexcept;

    template <class T> T CopyAndParseRecord(Record* pRecordHeader, uint32_t aResolvedFormIdPrefix);

    template <class T> void ParseGRUP(Record* pRecordHeader, T& aRecord);

    String m_filename = "";
    Buffer m_buffer{};

    union
    {
        uint8_t m_standardId = 0;
        uint16_t m_liteId;
    };
    uint32_t m_formIdPrefix = 0;
    bool m_setupValid = false;

    TiltedPhoques::Map<String, uint32_t>& m_masterFiles;
    TiltedPhoques::Map<uint8_t, uint32_t> m_parentToFormIdPrefix{};
    // Resolved form id of the CELL whose children group is being read; zero outside one.
    uint32_t m_currentCell{};
};

} // namespace ESLoader
