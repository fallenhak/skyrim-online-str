#include "RACE.h"

#include <ESLoader.h>

bool RACE::ParseChunks(const uint8_t* apRecordData, Map<uint8_t, uint32_t>&) noexcept
{
    bool fieldsValid = true;
    const auto* const pChunkData = apRecordData + sizeof(Record);
    const bool chunksValid = IterateChunksBounded(pChunkData, GetDataSize(),
        [&](ChunkId aChunkId, Buffer::Reader& aReader, const size_t aChunkSize)
        {
            switch (aChunkId)
            {
            case ChunkId::EDID_ID:
                if (!ESLoader::ReadZString(aReader, aChunkSize, m_editorId))
                    fieldsValid = false;
                break;
            }
        });

    return chunksValid && fieldsValid;
}
