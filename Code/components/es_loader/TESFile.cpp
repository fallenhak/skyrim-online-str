#include "TESFile.h"
#include "PluginFilename.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <system_error>

namespace ESLoader
{
namespace
{
constexpr size_t kMaximumGroupDepth = 64;
}

TESFile::TESFile(Map<String, uint32_t>& aMasterFiles)
    : m_masterFiles(aMasterFiles)
{
}

bool TESFile::Setup(uint8_t aStandardId)
{
    m_setupValid = false;
    if (aStandardId > kMaxStandardPluginId)
    {
        spdlog::warn("Plugin {} has an out-of-range standard load-order ID: {}", m_filename, static_cast<uint32_t>(aStandardId));
        return false;
    }

    m_standardId = aStandardId;
    m_formIdPrefix = static_cast<uint32_t>(m_standardId) << 24;
    m_setupValid = true;
    return true;
}

bool TESFile::Setup(uint16_t aLiteId)
{
    m_setupValid = false;
    if (aLiteId > kMaxLitePluginId)
    {
        spdlog::warn("Plugin {} has an out-of-range light load-order ID: {}", m_filename, aLiteId);
        return false;
    }

    m_liteId = aLiteId;
    m_formIdPrefix = 0xFE000000u | (static_cast<uint32_t>(m_liteId) << 12);
    m_setupValid = true;
    return true;
}

std::optional<uint32_t> TESFile::ReadHeaderFlags(const std::filesystem::path& acPath) noexcept
{
    std::error_code fileSizeError;
    const uintmax_t fileSize = std::filesystem::file_size(acPath, fileSizeError);
    if (fileSizeError || fileSize < sizeof(Record))
        return std::nullopt;

    std::ifstream file(acPath, std::ios::binary);
    if (!file)
        return std::nullopt;

    std::array<uint8_t, sizeof(Record)> header{};
    file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (file.gcount() != static_cast<std::streamsize>(header.size()))
        return std::nullopt;

    uint32_t formType = 0;
    uint32_t dataSize = 0;
    uint32_t flags = 0;
    std::memcpy(&formType, header.data(), sizeof(formType));
    std::memcpy(&dataSize, header.data() + sizeof(formType), sizeof(dataSize));
    std::memcpy(&flags, header.data() + sizeof(formType) + sizeof(dataSize), sizeof(flags));

    if (formType != static_cast<uint32_t>(FormEnum::TES4) ||
        static_cast<uintmax_t>(dataSize) > fileSize - sizeof(Record))
    {
        return std::nullopt;
    }

    return flags;
}

bool TESFile::LoadFile(const std::filesystem::path& acPath) noexcept
{
    m_filename = acPath.filename().string();

    std::error_code fileSizeError;
    const uintmax_t fileSize = std::filesystem::file_size(acPath, fileSizeError);
    if (fileSizeError)
    {
        spdlog::warn("Failed to stat plugin {}: {}", m_filename, fileSizeError.message());
        return false;
    }

    m_buffer.Resize(fileSize);

    std::ifstream file(acPath, std::ios::binary);
    if (file.fail())
    {
        spdlog::error("Failed to open plugin {}", m_filename);
        return false;
    }

    if (fileSize != 0)
    {
        file.read(reinterpret_cast<char*>(m_buffer.GetWriteData()), static_cast<std::streamsize>(fileSize));
        if (file.gcount() != static_cast<std::streamsize>(fileSize))
        {
            spdlog::warn("Failed to read plugin {} completely", m_filename);
            return false;
        }
    }

    return true;
}

bool TESFile::IndexRecords(RecordCollection& aRecordCollection) noexcept
{
    if (m_filename.size() == 0)
        return false;

    if (!InitializeFormIdPrefixes())
        return false;

    Buffer::Reader reader(&m_buffer);
    while (reader.GetBytePosition() < m_buffer.GetSize())
    {
        if (!ReadGroupOrRecord(reader, aRecordCollection, m_buffer.GetSize(), 0))
        {
            spdlog::warn("Plugin {} contains a truncated or malformed record/group; stopping record indexing", m_filename);
            return false;
        }
    }

    return true;
}

bool TESFile::InitializeFormIdPrefixes() noexcept
{
    m_parentToFormIdPrefix.clear();

    if (!m_setupValid)
    {
        spdlog::warn("Plugin {} has no valid load-order ID", m_filename);
        return false;
    }

    if (m_buffer.GetSize() < sizeof(Record))
    {
        spdlog::warn("Plugin {} has no complete TES4 header", m_filename);
        return false;
    }

    const auto* const pFileHeader = m_buffer.GetWriteData();
    uint32_t formType = 0;
    uint32_t dataSize = 0;
    std::memcpy(&formType, pFileHeader, sizeof(formType));
    std::memcpy(&dataSize, pFileHeader + sizeof(formType), sizeof(dataSize));
    if (formType != static_cast<uint32_t>(FormEnum::TES4) || dataSize > m_buffer.GetSize() - sizeof(Record))
    {
        spdlog::warn("Plugin {} has an invalid TES4 header", m_filename);
        return false;
    }

    TES4 fileHeader;
    fileHeader.CopyRecordData(pFileHeader);
    if (!fileHeader.ParseChunks(pFileHeader, m_parentToFormIdPrefix))
    {
        spdlog::warn("Plugin {} has malformed TES4 chunks", m_filename);
        return false;
    }

    // Each MAST entry needs one parent slot and the plugin itself needs the
    // next slot. Parent indices are one byte, so 255 masters is the maximum
    // count that still leaves a distinct slot for this plugin.
    if (fileHeader.m_masterFiles.size() > std::numeric_limits<uint8_t>::max())
    {
        spdlog::warn("Plugin {} has too many masters for distinct parent slots", m_filename);
        return false;
    }

    uint16_t parentId = 0;
    std::set<String> seenMasterFilenames;
    for (const Chunks::MAST& master : fileHeader.m_masterFiles)
    {
        // An unresolved master must fail closed; operator[] would silently
        // turn it into standard prefix zero.
        String masterFilenameKey;
        if (!GetPluginFilenameKey(master.m_masterName, masterFilenameKey))
        {
            spdlog::warn("Plugin {} references invalid master {}; skipping its records", m_filename, master.m_masterName);
            m_parentToFormIdPrefix.clear();
            return false;
        }

        if (!seenMasterFilenames.emplace(masterFilenameKey).second)
        {
            spdlog::warn("Plugin {} has duplicate master {}; skipping its records", m_filename, master.m_masterName);
            m_parentToFormIdPrefix.clear();
            return false;
        }

        const auto masterId = m_masterFiles.find(masterFilenameKey);
        if (masterId == std::end(m_masterFiles))
        {
            spdlog::warn("Plugin {} references unresolved master {}; skipping its records", m_filename, master.m_masterName);
            m_parentToFormIdPrefix.clear();
            return false;
        }

        m_parentToFormIdPrefix[static_cast<uint8_t>(parentId++)] = masterId->second;
    }

    m_parentToFormIdPrefix[static_cast<uint8_t>(parentId)] = m_formIdPrefix;
    return true;
}

bool TESFile::ReadGroupOrRecord(
    Buffer::Reader& aReader, RecordCollection& aRecordCollection, const size_t aParentEnd, const size_t aGroupDepth) noexcept
{
    const size_t recordPosition = aReader.GetBytePosition();
    if (recordPosition > aParentEnd || aParentEnd - recordPosition < sizeof(uint32_t) * 2)
        return false;

    uint32_t type = 0;
    uint32_t size = 0;
    const auto* const pRecordBytes = m_buffer.GetWriteData() + recordPosition;
    std::memcpy(&type, pRecordBytes, sizeof(type));
    std::memcpy(&size, pRecordBytes + sizeof(type), sizeof(size));

    if (type == static_cast<uint32_t>(FormEnum::GRUP))
    {
        if (aGroupDepth >= kMaximumGroupDepth || size < sizeof(Group) || size > aParentEnd - recordPosition)
            return false;

        const size_t endOfGroup = recordPosition + size;
        aReader.Advance(sizeof(Group));

        while (aReader.GetBytePosition() < endOfGroup)
        {
            if (!ReadGroupOrRecord(aReader, aRecordCollection, endOfGroup, aGroupDepth + 1))
                return false;
        }

        return aReader.GetBytePosition() == endOfGroup;
    }

    if (aParentEnd - recordPosition < sizeof(Record) || size > aParentEnd - recordPosition - sizeof(Record))
        return false;

    uint32_t formId = 0;
    std::memcpy(&formId, pRecordBytes + 12, sizeof(formId));

    // The complete header and declared payload are inside the current group/file
    // before any typed record view or chunk reader is formed.
    Record* pRecord = reinterpret_cast<Record*>(m_buffer.GetWriteData() + recordPosition);
    const FormEnum formType = static_cast<FormEnum>(type);
    const auto formIdPrefix = GetFormIdPrefix(formId, m_parentToFormIdPrefix);
    const auto parentFormIdPrefix = m_parentToFormIdPrefix.find(static_cast<uint8_t>(formId >> 24));
    const bool hasOutOfRangeLightLocalId =
        parentFormIdPrefix != std::end(m_parentToFormIdPrefix) &&
        (parentFormIdPrefix->second & 0xFF000000u) == 0xFE000000u && (formId & 0x00FFFFFFu) > 0x00000FFFu;
    const bool isActorPopulationRecord = formType == FormEnum::ACHR || formType == FormEnum::NPC_ || formType == FormEnum::RACE;

    if (hasOutOfRangeLightLocalId || (isActorPopulationRecord && !formIdPrefix))
    {
        spdlog::warn("Plugin {} has an invalid or unresolved form ID {:X}; skipping record", m_filename, formId);
        aReader.Advance(sizeof(Record) + size);
        return true;
    }
    const uint32_t resolvedFormIdPrefix = formIdPrefix.value_or(0);
    bool actorRecordValid = true;

    switch (formType)
    {
        case FormEnum::TES4:
        {
            break;
        }
        case FormEnum::ACHR:
        {
            ACHR parsedRecord;
            parsedRecord.CopyRecordData(pRecord);
            parsedRecord.SetBaseId(resolvedFormIdPrefix);
            actorRecordValid = parsedRecord.ParseChunks(pRecordBytes, m_parentToFormIdPrefix);
            if (actorRecordValid)
                aRecordCollection.m_actorReferences[parsedRecord.GetFormId()] = parsedRecord;
            else
                aRecordCollection.m_actorReferences.erase(resolvedFormIdPrefix + (formId & 0x00FFFFFFu));
            break;
        }
        case FormEnum::REFR:
        {
            REFR parsedRecord = CopyAndParseRecord<REFR>(pRecord, resolvedFormIdPrefix);
            aRecordCollection.m_objectReferences[parsedRecord.GetFormId()] = parsedRecord;
            break;
        }
        case FormEnum::CELL: break;
        case FormEnum::CLMT:
        {
            CLMT parsedRecord = CopyAndParseRecord<CLMT>(pRecord, resolvedFormIdPrefix);
            aRecordCollection.m_climates[parsedRecord.GetFormId()] = parsedRecord;
            break;
        }
        case FormEnum::NPC_:
        {
            NPC parsedRecord;
            parsedRecord.CopyRecordData(pRecord);
            parsedRecord.SetBaseId(resolvedFormIdPrefix);
            actorRecordValid = parsedRecord.ParseChunks(pRecordBytes, m_parentToFormIdPrefix);
            if (actorRecordValid)
                aRecordCollection.m_npcs[parsedRecord.GetFormId()] = parsedRecord;
            else
                aRecordCollection.m_npcs.erase(resolvedFormIdPrefix + (formId & 0x00FFFFFFu));
            break;
        }
        case FormEnum::RACE:
        {
            RACE parsedRecord;
            parsedRecord.CopyRecordData(pRecord);
            parsedRecord.SetBaseId(resolvedFormIdPrefix);
            actorRecordValid = parsedRecord.ParseChunks(pRecordBytes, m_parentToFormIdPrefix);
            if (actorRecordValid)
                aRecordCollection.m_races[parsedRecord.GetFormId()] = parsedRecord;
            else
                aRecordCollection.m_races.erase(resolvedFormIdPrefix + (formId & 0x00FFFFFFu));
            break;
        }
        case FormEnum::CONT:
        {
            CONT parsedRecord = CopyAndParseRecord<CONT>(pRecord, resolvedFormIdPrefix);
            aRecordCollection.m_containers[parsedRecord.GetFormId()] = parsedRecord;
            break;
        }
        case FormEnum::GMST:
        {
            GMST parsedRecord = CopyAndParseRecord<GMST>(pRecord, resolvedFormIdPrefix);
            aRecordCollection.m_gameSettings[parsedRecord.GetFormId()] = parsedRecord;
            break;
        }
        case FormEnum::WRLD:
        {
            WRLD parsedRecord = CopyAndParseRecord<WRLD>(pRecord, resolvedFormIdPrefix);
            aRecordCollection.m_worlds[parsedRecord.GetFormId()] = parsedRecord;
        }
        case FormEnum::NAVM:
        {
            NAVM parsedRecord = CopyAndParseRecord<NAVM>(pRecord, resolvedFormIdPrefix);
            aRecordCollection.m_navMeshes[parsedRecord.GetFormId()] = parsedRecord;
        }
        }

    if (!actorRecordValid)
        spdlog::warn("Plugin {} has malformed actor population record {:X}; skipping record", m_filename, formId);
    else if (formType != FormEnum::TES4)
    {
        Record record;
        record.CopyRecordData(pRecord);
        aRecordCollection.m_allRecords[formId] = record;
    }

    aReader.Advance(sizeof(Record) + size);

    return true;
}

template <typename T>
concept ExpectsGRUP = requires(T t) { &T::ParseGRUP; };

template <class T> T TESFile::CopyAndParseRecord(Record* pRecordHeader, const uint32_t aResolvedFormIdPrefix)
{
    T* pRecord = reinterpret_cast<T*>(pRecordHeader);

    T parsedRecord;
    parsedRecord.CopyRecordData(pRecord);
    parsedRecord.SetBaseId(aResolvedFormIdPrefix);
    parsedRecord.ParseChunks(*pRecord, m_parentToFormIdPrefix);

    // If the record expects a subgroup right after, parse it? Or do we not care since we load everything?
    if constexpr (ExpectsGRUP<T>)
    {
        //    ParseGRUP(pRecord, parsedRecord);
    }

    return parsedRecord;
}

template <class T> void TESFile::ParseGRUP(Record* pRecordHeader, T& aRecord)
{
    // aRecord.ParseGRUP();
}

std::optional<uint32_t> TESFile::GetFormIdPrefix(uint32_t aFormId, Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept
{
    auto baseId = (uint8_t)(aFormId >> 24);
    const auto masterId = aParentToFormIdPrefix.find(baseId);

    if (masterId == std::end(aParentToFormIdPrefix))
    {
        // TODO: this is weird, but for some reason, in Skyrim.esm,
        // the GMST record with EDID "iDaysToRespawnVendor" has a base id of 0x01
        spdlog::warn("Form id prefix not found: {:X}", baseId);
        return std::nullopt;
    }

    const uint32_t localFormId = aFormId & 0x00FFFFFFu;
    if ((masterId->second & 0xFF000000u) == 0xFE000000u && localFormId > 0x00000FFFu)
    {
        spdlog::warn("Light-plugin form ID has an out-of-range local ID: {:X}", aFormId);
        return std::nullopt;
    }

    return masterId->second;
}

} // namespace ESLoader
