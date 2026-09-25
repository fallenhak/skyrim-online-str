#include "LVLI.h"

#include <ESLoader.h>

bool LVLI::ParseChunks(const uint8_t* apRecordData, Map<uint8_t, uint32_t>& aParentToFormIdPrefix) noexcept
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
            case ChunkId::LVLD_ID:
                if (aChunkSize < 1)
                    fieldsValid = false;
                else
                    aReader.ReadBytes(&m_chanceNone, 1);
                break;
            case ChunkId::LVLF_ID:
                if (aChunkSize < 1)
                    fieldsValid = false;
                else
                    aReader.ReadBytes(&m_flags, 1);
                break;
            case ChunkId::LVLG_ID:
                if (aChunkSize < 4)
                    fieldsValid = false;
                else
                    m_chanceNoneGlobal = Chunks::ReadFormId(aReader, aParentToFormIdPrefix);
                break;
            case ChunkId::LVLO_ID:
                // LVLO: uint16 level, uint16 padding, formid reference, uint16 count, uint16 padding.
                if (aChunkSize < 10)
                    fieldsValid = false;
                else
                {
                    Entry entry{};
                    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&entry.Level), 2);
                    aReader.Advance(2);
                    entry.FormId = Chunks::ReadFormId(aReader, aParentToFormIdPrefix);
                    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&entry.Count), 2);
                    m_entries.push_back(entry);
                }
                break;
            }
        });

    return chunksValid && fieldsValid;
}
