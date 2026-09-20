#include "RACE.h"

#include <ESLoader.h>

void RACE::ParseChunks(RACE& aSourceRecord, Map<uint8_t, uint32_t>&) noexcept
{
    aSourceRecord.IterateChunks(
        [&](ChunkId aChunkId, Buffer::Reader& aReader)
        {
            switch (aChunkId)
            {
            case ChunkId::EDID_ID: m_editorId = ESLoader::ReadZString(aReader); break;
            }
        });
}
