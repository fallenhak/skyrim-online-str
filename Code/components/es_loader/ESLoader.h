#pragma once

#include <TESFile.h>

namespace fs = std::filesystem;

namespace ESLoader
{
struct RecordCollection;

struct PluginData
{
    [[nodiscard]] bool IsLite() const noexcept { return m_isLite; }

    String m_filename;
    union
    {
        uint8_t m_standardId = 0;
        uint16_t m_liteId;
    };
    bool m_isLite = false;
};
using PluginCollection = Vector<PluginData>;

String ReadZString(Buffer::Reader& aReader) noexcept;
bool ReadZString(Buffer::Reader& aReader, size_t aChunkSize, String& aOutput);
String ReadWString(Buffer::Reader& aReader) noexcept;

class ESLoader
{
public:
    ESLoader();
    explicit ESLoader(fs::path aDirectory);

    UniquePtr<RecordCollection> BuildRecordCollection(bool aLoadRecords = false) noexcept;

    PluginCollection& GetLoadOrder() noexcept { return m_loadOrder; }
    const PluginCollection& GetLoadOrder() const noexcept { return m_loadOrder; }

private:
    bool LoadLoadOrder();
    UniquePtr<RecordCollection> LoadFiles();

    fs::path GetPath(const String& acFilename) const;

    fs::path m_directory = "";
    Vector<PluginData> m_loadOrder{};
    // Server form prefixes keyed by the filenames records use in MAST.
    TiltedPhoques::Map<String, uint32_t> m_masterFiles{};
};
} // namespace ESLoader
