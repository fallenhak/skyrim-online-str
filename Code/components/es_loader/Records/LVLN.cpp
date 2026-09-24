#include "LVLN.h"

#include <ESLoader.h>

bool LVLN::ParseChunks(const uint8_t* apRecordData, Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept
{
    // Population identity only needs the entry references; level, count and
    // COED extra data are skipped while chunk boundaries are still validated.
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
            case ChunkId::LVLO_ID:
                // LVLO: uint16 level, uint16 padding, formid reference, uint16 count, uint16 padding.
                if (aChunkSize < 8)
                    fieldsValid = false;
                else
                {
                    aReader.Advance(4);
                    m_entryIds.push_back(Chunks::ReadFormId(aReader, aParentToFormIdPrefix));
                }
                break;
            }
        });

    return chunksValid && fieldsValid;
}
