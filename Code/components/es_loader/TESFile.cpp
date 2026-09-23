#include "TESFile.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace ESLoader
{
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

    while (true)
    {
        if (!ReadGroupOrRecord(reader, aRecordCollection))
            break;
    }

    return true;
}

bool TESFile::InitializeFormIdPrefixes() noexcept
{
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

    auto* pFileHeader = reinterpret_cast<TES4*>(m_buffer.GetWriteData());
    if (pFileHeader->GetType() != FormEnum::TES4 || pFileHeader->GetDataSize() > m_buffer.GetSize() - sizeof(Record))
    {
        spdlog::warn("Plugin {} has an invalid TES4 header", m_filename);
        return false;
    }

    TES4 fileHeader;
    fileHeader.CopyRecordData(*pFileHeader);
    fileHeader.ParseChunks(*pFileHeader, m_parentToFormIdPrefix);

    uint8_t parentId = 0;
    for (const Chunks::MAST& master : fileHeader.m_masterFiles)
    {
        // An unresolved master must fail closed; operator[] would silently
        // turn it into standard prefix zero.
        const auto masterId = m_masterFiles.find(master.m_masterName);
        if (masterId == std::end(m_masterFiles))
        {
            spdlog::warn("Plugin {} references unresolved master {}; skipping its records", m_filename, master.m_masterName);
            m_parentToFormIdPrefix.clear();
            return false;
        }

        m_parentToFormIdPrefix[parentId++] = masterId->second;
    }

    m_parentToFormIdPrefix[parentId] = m_formIdPrefix;
    return true;
}

bool TESFile::ReadGroupOrRecord(Buffer::Reader& aReader, RecordCollection& aRecordCollection) noexcept
{
    if (aReader.Eof())
        return false;

    uint32_t type = 0;
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&type), 4);
    uint32_t size = 0;
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&size), 4);
    aReader.Reverse(8);

    if (type == static_cast<uint32_t>(FormEnum::GRUP))
    {
        const size_t endOfGroup = aReader.GetBytePosition() + size;
        aReader.Advance(sizeof(Group));

        while (aReader.GetBytePosition() < endOfGroup)
        {
            ReadGroupOrRecord(aReader, aRecordCollection);
        }
    }
    else // Records
    {
        Record* pRecord = reinterpret_cast<Record*>(m_buffer.GetWriteData() + aReader.GetBytePosition());
        const auto formIdPrefix = GetFormIdPrefix(pRecord->GetFormId(), m_parentToFormIdPrefix);

        if ((pRecord->GetType() == FormEnum::ACHR || pRecord->GetType() == FormEnum::NPC_ || pRecord->GetType() == FormEnum::RACE) &&
            !formIdPrefix)
        {
            spdlog::warn("Plugin {} has an unresolved actor-population record prefix for form {:X}; skipping record", m_filename, pRecord->GetFormId());
            aReader.Advance(sizeof(Record) + size);
            return true;
        }
        const uint32_t resolvedFormIdPrefix = formIdPrefix.value_or(0);

        switch (pRecord->GetType())
        {
        case FormEnum::TES4:
        {
            break;
        }
        case FormEnum::ACHR:
        {
            ACHR parsedRecord = CopyAndParseRecord<ACHR>(pRecord, resolvedFormIdPrefix);
            aRecordCollection.m_actorReferences[parsedRecord.GetFormId()] = parsedRecord;
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
            NPC parsedRecord = CopyAndParseRecord<NPC>(pRecord, resolvedFormIdPrefix);
            aRecordCollection.m_npcs[parsedRecord.GetFormId()] = parsedRecord;
            break;
        }
        case FormEnum::RACE:
        {
            RACE parsedRecord = CopyAndParseRecord<RACE>(pRecord, resolvedFormIdPrefix);
            aRecordCollection.m_races[parsedRecord.GetFormId()] = parsedRecord;
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

        // pRecord->DiscoverChunks();

        if (pRecord->GetType() != FormEnum::TES4)
        {
            Record record;
            record.CopyRecordData(*pRecord);
            record.SetBaseId(resolvedFormIdPrefix);
            aRecordCollection.m_allRecords[pRecord->GetFormId()] = *pRecord;
        }

        aReader.Advance(sizeof(Record) + size);
    }

    return true;
}

template <typename T>
concept ExpectsGRUP = requires(T t) { &T::ParseGRUP; };

template <class T> T TESFile::CopyAndParseRecord(Record* pRecordHeader, const uint32_t aResolvedFormIdPrefix)
{
    T* pRecord = reinterpret_cast<T*>(pRecordHeader);

    T parsedRecord;
    parsedRecord.CopyRecordData(*pRecord);
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

    return masterId->second;
}

} // namespace ESLoader
