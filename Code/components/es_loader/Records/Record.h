#pragma once

#include "TESFileRecordTypes.inl"

class Record
{
public:
#pragma pack(push, 1)
    struct Chunk
    {
        ChunkId m_chunkId;
        uint16_t m_dataSize;
    };
#pragma pack(pop)

    enum FLAGS
    {
        kMasterFile = 1,
        // TES4 header flag 0x00000200 marks a plugin as light/ESL. This is
        // authoritative for the plugin namespace even when the filename has
        // an .esp extension.
        kESL = 0x200,
        kCompressed = 0x40000,
        kIgnored = 0x1000,
        kIsMarker = 0x800000,
    };

    Record() = default;

    void CopyRecordData(const void* apRecordData);
    void SetBaseId(uint32_t aBaseId);

    using BoundedChunkCallback = std::function<void(ChunkId, Buffer::Reader&, size_t)>;

    void IterateChunks(const std::function<void(ChunkId, Buffer::Reader&)>& aCallback);
    [[nodiscard]] bool IterateChunksBounded(const BoundedChunkCallback& aCallback);
    [[nodiscard]] bool IterateChunksBounded(const uint8_t* apChunkData, size_t aDataSize, const BoundedChunkCallback& aCallback);
    [[nodiscard]] bool DecompressChunkData(const void* apCompressedData, size_t aCompressedSize, void* apDecompressedData, size_t aDecompressedSize);

    void DiscoverChunks();

    [[nodiscard]] FormEnum GetType() const noexcept { return m_formType; }
    [[nodiscard]] uint32_t GetFormId() const noexcept { return m_formId; }
    [[nodiscard]] uint32_t GetDataSize() const noexcept { return m_dataSize; }
    [[nodiscard]] uint32_t GetFlags() const noexcept { return m_flags; }

    [[nodiscard]] bool Compressed() const noexcept { return (m_flags & FLAGS::kCompressed) != 0; }
    [[nodiscard]] bool Ignored() const noexcept { return (m_flags & FLAGS::kIgnored) != 0; }
    [[nodiscard]] bool Master() const noexcept { return (m_flags & FLAGS::kMasterFile) != 0; }

    [[nodiscard]] bool DefaultForm() const noexcept { return m_formId - 1 <= 0x7FE; }

private:
    FormEnum m_formType;
    uint32_t m_dataSize;
    uint32_t m_flags;
    uint32_t m_formId;
    uint32_t m_versionControl;
    uint16_t m_formVersion;
    uint16_t m_vcVersion;
};

static_assert(sizeof(Record) == 0x18);
static_assert(sizeof(Record::Chunk) == 0x6);
