#include "LCTN.h"

bool LCTN::ParseChunks(const uint8_t* apRecordData, Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept
{
    bool fieldsValid = true;
    const auto* const pChunkData = apRecordData + sizeof(Record);
    const bool chunksValid = IterateChunksBounded(pChunkData, GetDataSize(),
        [&](ChunkId aChunkId, Buffer::Reader& aReader, const size_t aChunkSize)
        {
            if (aChunkId != ChunkId::PNAM_ID)
                return;
            if (aChunkSize < 4)
                fieldsValid = false;
            else
                m_parentLocation = Chunks::ReadFormId(aReader, aParentToFormIdPrefix);
        });

    return chunksValid && fieldsValid;
}
