#include "ACHR.h"

void ACHR::ParseChunks(ACHR& aSourceRecord, Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept
{
    aSourceRecord.IterateChunks(
        [&](ChunkId aChunkId, Buffer::Reader& aReader)
        {
            if (aChunkId == ChunkId::NAME_ID)
                m_baseObject = Chunks::NAME(aReader, aParentToFormIdPrefix);
        });
}
