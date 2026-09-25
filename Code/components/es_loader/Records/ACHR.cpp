#include "ACHR.h"

bool ACHR::ParseChunks(const uint8_t* apRecordData, Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept
{
    bool fieldsValid = true;
    const auto* const pChunkData = apRecordData + sizeof(Record);
    const bool chunksValid = IterateChunksBounded(pChunkData, GetDataSize(),
        [&](ChunkId aChunkId, Buffer::Reader& aReader, const size_t aChunkSize)
        {
            if (aChunkId == ChunkId::XEZN_ID)
            {
                if (aChunkSize < sizeof(uint32_t))
                {
                    fieldsValid = false;
                    return;
                }
                m_encounterZone = Chunks::ReadFormId(aReader, aParentToFormIdPrefix);
                return;
            }

            if (aChunkId == ChunkId::NAME_ID)
            {
                if (aChunkSize < sizeof(uint32_t))
                {
                    fieldsValid = false;
                    return;
                }
                m_baseObject = Chunks::NAME(aReader, aParentToFormIdPrefix);
            }
        });

    return chunksValid && fieldsValid;
}
