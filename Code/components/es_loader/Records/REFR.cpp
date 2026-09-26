#include "REFR.h"

void REFR::ParseChunks(REFR& aSourceRecord, Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept
{
    aSourceRecord.IterateChunks(
        [&](ChunkId aChunkId, Buffer::Reader& aReader)
        {
            switch (aChunkId)
            {
            case ChunkId::NAME_ID: m_basicObject = Chunks::NAME(aReader, aParentToFormIdPrefix); break;
            case ChunkId::XMRK_ID:
                // XMRK contains no data
                m_markerData.m_isMarker = true;
                break;
            case ChunkId::FNAM_ID: aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_markerData.m_flags), 1); break;
            case ChunkId::TNAM_ID: aReader.ReadBytes(reinterpret_cast<uint8_t*>(&m_markerData.m_marker), 2); break;
            case ChunkId::XLOC_ID:
                // XLOC: uint8 level, 3 bytes padding, formid key, ...
                m_isLocked = true;
                aReader.ReadBytes(&m_lockLevel, 1);
                aReader.Advance(3);
                m_lockKey = Chunks::ReadFormId(aReader, aParentToFormIdPrefix);
                break;
            case ChunkId::XEZN_ID: m_encounterZone = Chunks::ReadFormId(aReader, aParentToFormIdPrefix); break;
            case ChunkId::XLIB_ID: m_leveledItemBase = Chunks::ReadFormId(aReader, aParentToFormIdPrefix); break;
            }
        });
}
