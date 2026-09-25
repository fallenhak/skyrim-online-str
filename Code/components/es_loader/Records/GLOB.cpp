#include "GLOB.h"

#include <ESLoader.h>

bool GLOB::ParseChunks(const uint8_t* apRecordData, Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept
{
    (void)aParentToFormIdPrefix;

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
            case ChunkId::FLTV_ID:
                if (aChunkSize < 4)
                    fieldsValid = false;
                else
                    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_value), 4);
                break;
            }
        });

    return chunksValid && fieldsValid;
}
