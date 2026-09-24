#include "TES4.h"

#include <ESLoader.h>

bool TES4::ParseChunks(const uint8_t* apRecordData, Map<uint8_t, uint32_t>&) noexcept
{
    bool fieldsValid = true;
    const auto* const pChunkData = apRecordData + sizeof(Record);
    const bool chunksValid = IterateChunksBounded(pChunkData, GetDataSize(),
        [&](ChunkId aChunkId, Buffer::Reader& aReader, const size_t aChunkSize)
        {
            switch (aChunkId)
            {
            case ChunkId::MAST_ID:
            {
                Chunks::MAST master;
                if (!ESLoader::ReadZString(aReader, aChunkSize, master.m_masterName))
                    fieldsValid = false;
                else
                    m_masterFiles.push_back(master);
                break;
            }
            }
        });

    return chunksValid && fieldsValid;
}
