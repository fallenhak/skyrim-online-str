#include "CELL.h"

bool CELL::ParseChunks(const uint8_t* apRecordData, Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept
{
    bool fieldsValid = true;
    const auto* const pChunkData = apRecordData + sizeof(Record);
    const bool chunksValid = IterateChunksBounded(pChunkData, GetDataSize(),
        [&](ChunkId aChunkId, Buffer::Reader& aReader, const size_t aChunkSize)
        {
            if (aChunkId != ChunkId::XEZN_ID && aChunkId != ChunkId::XLCN_ID)
                return;
            if (aChunkSize < 4)
            {
                fieldsValid = false;
                return;
            }

            const uint32_t formId = Chunks::ReadFormId(aReader, aParentToFormIdPrefix);
            if (aChunkId == ChunkId::XEZN_ID)
                m_encounterZone = formId;
            else
                m_location = formId;
        });

    return chunksValid && fieldsValid;
}
