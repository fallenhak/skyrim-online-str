#include "NPC.h"

#include <ESLoader.h>

bool NPC::ParseChunks(const uint8_t* apRecordData, Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept
{
    // Population identity uses EDID and RNAM; skip variable VMAD and unrelated NPC
    // payloads while the shared iterator still validates their chunk boundaries.
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
            case ChunkId::RNAM_ID:
                if (aChunkSize < sizeof(uint32_t))
                    fieldsValid = false;
                else
                    m_raceId = Chunks::ReadFormId(aReader, aParentToFormIdPrefix);
                break;
            }
        });

    return chunksValid && fieldsValid;
}
