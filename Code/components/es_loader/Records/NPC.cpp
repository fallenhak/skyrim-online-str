#include "NPC.h"

#include <ESLoader.h>

bool NPC::ParseChunks(const uint8_t* apRecordData, Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept
{
    // Population identity uses EDID, RNAM, TPLT and the ACBS template flags; skip variable VMAD and unrelated NPC
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
            case ChunkId::TPLT_ID:
                if (aChunkSize < sizeof(uint32_t))
                    fieldsValid = false;
                else
                    m_templateId = Chunks::ReadFormId(aReader, aParentToFormIdPrefix);
                break;
            case ChunkId::ACBS_ID:
                // Only the template data flags (offset 0x10) matter for population identity.
                if (aChunkSize < sizeof(Chunks::ACBS))
                    fieldsValid = false;
                else
                {
                    aReader.Advance(0x10);
                    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_templateDataFlags), sizeof(m_templateDataFlags));
                }
                break;
            }
        });

    return chunksValid && fieldsValid;
}
