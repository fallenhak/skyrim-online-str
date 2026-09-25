#include "ECZN.h"

#include <ESLoader.h>

bool ECZN::ParseChunks(const uint8_t* apRecordData, Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept
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
            case ChunkId::DATA_ID:
                // DATA: formid owner, formid location, int8 rank, int8 min level, uint8 flags, int8 max level.
                if (aChunkSize < 12)
                    fieldsValid = false;
                else
                {
                    m_owner = Chunks::ReadFormId(aReader, aParentToFormIdPrefix);
                    m_location = Chunks::ReadFormId(aReader, aParentToFormIdPrefix);
                    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_rank), 1);
                    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_minLevel), 1);
                    aReader.ReadBytes(&m_flags, 1);
                    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_maxLevel), 1);
                }
                break;
            }
        });

    return chunksValid && fieldsValid;
}
