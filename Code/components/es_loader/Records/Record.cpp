#include "Record.h"

#include <cstring>
#include <limits>

#include <zlib.h>

namespace
{
constexpr size_t kMaximumDecompressedRecordSize = 64u * 1024u * 1024u;
}

void Record::CopyRecordData(const void* apRecordData)
{
    std::memcpy(this, apRecordData, sizeof(Record));
}

void Record::SetBaseId(uint32_t aBaseId)
{
    m_formId &= 0x00FFFFFF;
    m_formId += aBaseId;
}

void Record::IterateChunks(const std::function<void(ChunkId, Buffer::Reader&)>& aCallback)
{
    static_cast<void>(IterateChunksBounded(
        [&](ChunkId aChunkId, Buffer::Reader& aReader, size_t) { aCallback(aChunkId, aReader); }));
}

bool Record::IterateChunksBounded(const BoundedChunkCallback& aCallback)
{
    return IterateChunksBounded(reinterpret_cast<uint8_t*>(this) + sizeof(Record), m_dataSize, aCallback);
}

bool Record::IterateChunksBounded(const uint8_t* apChunkData, const size_t aDataSize, const BoundedChunkCallback& aCallback)
{
    if (aDataSize != m_dataSize || (aDataSize != 0 && apChunkData == nullptr))
        return false;

    const uint8_t* pChunkData = apChunkData;
    size_t chunkDataSize = aDataSize;
    Buffer decompressedData;

    if (Compressed())
    {
        if (m_dataSize < sizeof(uint32_t))
            return false;

        uint32_t uncompressedSize = 0;
        std::memcpy(&uncompressedSize, pChunkData, sizeof(uncompressedSize));
        if (uncompressedSize > kMaximumDecompressedRecordSize)
            return false;

        const size_t compressedSize = static_cast<size_t>(m_dataSize) - sizeof(uncompressedSize);
        if (compressedSize == 0)
            return false;

        try
        {
            decompressedData.Resize(uncompressedSize);
        }
        catch (...)
        {
            return false;
        }
        auto* pOutput = uncompressedSize != 0 ? decompressedData.GetWriteData() : nullptr;
        if (!DecompressChunkData(pChunkData + sizeof(uncompressedSize), compressedSize, pOutput, uncompressedSize))
            return false;

        pChunkData = decompressedData.GetWriteData();
        chunkDataSize = uncompressedSize;
    }

    size_t offset = 0;
    uint32_t largeDataSize = 0;
    bool hasLargeDataSize = false;

    while (offset < chunkDataSize)
    {
        if (chunkDataSize - offset < sizeof(Chunk))
            return false;

        Chunk chunkHeader{};
        std::memcpy(&chunkHeader, pChunkData + offset, sizeof(chunkHeader));
        const size_t payloadOffset = offset + sizeof(Chunk);
        const size_t remainingPayload = chunkDataSize - payloadOffset;

        if (chunkHeader.m_chunkId == ChunkId::XXXX_ID)
        {
            if (hasLargeDataSize || chunkHeader.m_dataSize != sizeof(uint32_t) || remainingPayload < sizeof(uint32_t))
                return false;

            std::memcpy(&largeDataSize, pChunkData + payloadOffset, sizeof(largeDataSize));
            if (largeDataSize == 0)
                return false;

            hasLargeDataSize = true;
            offset = payloadOffset + sizeof(largeDataSize);
            continue;
        }

        size_t payloadSize = chunkHeader.m_dataSize;
        if (hasLargeDataSize)
        {
            if (chunkHeader.m_dataSize != 0)
                return false;
            payloadSize = largeDataSize;
            hasLargeDataSize = false;
            largeDataSize = 0;
        }

        if (payloadSize > remainingPayload)
            return false;

        Buffer chunkBuffer(const_cast<uint8_t*>(pChunkData + payloadOffset), payloadSize);
        Buffer::Reader chunkReader(&chunkBuffer);
        aCallback(chunkHeader.m_chunkId, chunkReader, payloadSize);
        offset = payloadOffset + payloadSize;
    }

    return !hasLargeDataSize;
}

bool Record::DecompressChunkData(const void* apCompressedData, size_t aCompressedSize, void* apDecompressedData, size_t aDecompressedSize)
{
    if (aCompressedSize > std::numeric_limits<uInt>::max() || aDecompressedSize > std::numeric_limits<uInt>::max())
        return false;

    z_stream compressionStream{};
    uint8_t emptyOutput = 0;
    compressionStream.next_in = reinterpret_cast<Bytef*>(const_cast<void*>(apCompressedData));
    compressionStream.avail_in = static_cast<uInt>(aCompressedSize);
    compressionStream.next_out = aDecompressedSize != 0 ? reinterpret_cast<Bytef*>(apDecompressedData) : &emptyOutput;
    // Give zlib one byte of scratch space for an empty expected output so it can
    // still reach Z_STREAM_END without writing into a null pointer.
    compressionStream.avail_out = aDecompressedSize != 0 ? static_cast<uInt>(aDecompressedSize) : 1u;

    if (inflateInit(&compressionStream) != Z_OK)
        return false;

    const int inflateResult = inflate(&compressionStream, Z_FINISH);
    const bool complete = inflateResult == Z_STREAM_END && compressionStream.total_in == aCompressedSize &&
                          compressionStream.total_out == aDecompressedSize;
    const int endResult = inflateEnd(&compressionStream);
    return complete && endResult == Z_OK;
}

void Record::DiscoverChunks()
{
    IterateChunks(
        [&](ChunkId aChunkId, Buffer::Reader& aReader)
        {
            switch (aChunkId)
            {
                /*
            case ChunkId::XMRK_ID:
                spdlog::info("XMRK found in form {:X}", m_formId);
                break;
                */
            }
        });
}
