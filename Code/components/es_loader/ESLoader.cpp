

#include "ESLoader.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <system_error>
#include <utility>

#include <Records/CLMT.h>
#include <Records/NPC.h>
#include <Records/REFR.h>

namespace ESLoader
{
namespace
{
bool IsWhitespace(const char aCharacter) noexcept
{
    return std::isspace(static_cast<unsigned char>(aCharacter)) != 0;
}

String NormalizeLoadOrderLine(String aLine, const bool aFirstLine)
{
    // loadorder.txt is commonly written as UTF-8 without a BOM, but accepting
    // a BOM on the first line avoids turning it into part of the first plugin
    // name when a mod manager emits one.
    if (aFirstLine && aLine.size() >= 3 && static_cast<unsigned char>(aLine[0]) == 0xEF &&
        static_cast<unsigned char>(aLine[1]) == 0xBB && static_cast<unsigned char>(aLine[2]) == 0xBF)
    {
        aLine.erase(0, 3);
    }

    auto first = aLine.begin();
    while (first != aLine.end() && IsWhitespace(*first))
        ++first;

    auto last = aLine.end();
    while (last != first && IsWhitespace(*(last - 1)))
        --last;

    return String(first, last);
}

String MakeFilenameKey(const String& acFilename)
{
    String key = acFilename;
    std::transform(
        key.begin(), key.end(), key.begin(), [](const char aCharacter) { return static_cast<char>(std::tolower(static_cast<unsigned char>(aCharacter))); });
    return key;
}

bool IsSafePluginFilename(const String& acFilename) noexcept
{
    return !acFilename.empty() && acFilename.find('/') == String::npos && acFilename.find('\\') == String::npos &&
           acFilename.find(':') == String::npos &&
           std::none_of(acFilename.begin(), acFilename.end(), [](const char aCharacter) {
               return std::iscntrl(static_cast<unsigned char>(aCharacter)) != 0;
           });
}

enum class PluginType : uint8_t
{
    kInvalid,
    kMaster,
    kStandard,
    kLite,
};

PluginType GetPluginType(const String& acFilename) noexcept
{
    const auto extensionStart = acFilename.rfind('.');
    if (extensionStart == String::npos || extensionStart == 0 || acFilename.size() - extensionStart != 4)
        return PluginType::kInvalid;

    String extension = acFilename.substr(extensionStart);
    std::transform(
        extension.begin(), extension.end(), extension.begin(), [](const char aCharacter) { return static_cast<char>(std::tolower(static_cast<unsigned char>(aCharacter))); });

    if (extension == ".esm")
        return PluginType::kMaster;
    if (extension == ".esp")
        return PluginType::kStandard;
    if (extension == ".esl")
        return PluginType::kLite;
    return PluginType::kInvalid;
}
} // namespace

String ReadZString(Buffer::Reader& aReader) noexcept
{
    String zstring = String(reinterpret_cast<const char*>(aReader.GetDataAtPosition()));
    aReader.Advance(zstring.size() + 1);
    return zstring;
}

String ReadWString(Buffer::Reader& aReader) noexcept
{
    uint16_t stringLength = 0;
    aReader.ReadBytes(reinterpret_cast<uint8_t*>(&stringLength), 2);
    String wstring = String(reinterpret_cast<const char*>(aReader.GetDataAtPosition()), stringLength);
    aReader.Advance(stringLength);
    return wstring;
}

ESLoader::ESLoader()
    : ESLoader(fs::current_path() / "Data")
{
}

ESLoader::ESLoader(fs::path aDirectory)
    : m_directory(std::move(aDirectory))
{
}

UniquePtr<RecordCollection> ESLoader::BuildRecordCollection(bool aLoadRecords) noexcept
{
    std::error_code directoryError;
    if (!fs::is_directory(m_directory, directoryError))
    {
        m_loadOrder.clear();
        m_masterFiles.clear();
        if (aLoadRecords)
            spdlog::warn("Actor population record loading unavailable: ESLoader Data directory not found at '{}'", m_directory.string());
        return nullptr;
    }

    if (!LoadLoadOrder())
    {
        if (aLoadRecords)
            spdlog::warn("Actor population record loading unavailable: ESLoader could not read loadorder.txt from '{}'", m_directory.string());
        return nullptr;
    }

    if (!aLoadRecords)
        return MakeUnique<RecordCollection>();

    auto recordCollection = LoadFiles();
    if (!recordCollection)
    {
        spdlog::warn("Actor population record loading unavailable: ESLoader could not create a RecordCollection");
        return nullptr;
    }

    recordCollection->BuildReferences();

    return recordCollection;
}

bool ESLoader::LoadLoadOrder()
{
    m_loadOrder.clear();
    m_masterFiles.clear();

    const auto loadOrderPath = m_directory / "loadorder.txt";
    std::ifstream loadOrderFile(loadOrderPath);
    if (!loadOrderFile)
    {
        spdlog::warn("Failed to open loadorder.txt at '{}'", loadOrderPath.string());
        return false;
    }

    uint8_t standardId = 0x0;
    uint16_t liteId = 0x0;
    std::set<String> seenFilenames;
    bool firstLine = true;
    String line;

    while (std::getline(loadOrderFile, line))
    {
        line = NormalizeLoadOrderLine(std::move(line), firstLine);
        firstLine = false;

        if (line.empty() || line.front() == '#')
            continue;

        if (!IsSafePluginFilename(line))
        {
            spdlog::warn("Ignoring unsafe plugin entry in loadorder.txt");
            continue;
        }

        const auto pluginType = GetPluginType(line);
        if (pluginType == PluginType::kInvalid)
        {
            spdlog::warn("Ignoring unrecognized plugin entry in loadorder.txt: {}", line);
            continue;
        }

        if (!seenFilenames.emplace(MakeFilenameKey(line)).second)
        {
            spdlog::warn("Ignoring duplicate plugin entry in loadorder.txt: {}", line);
            continue;
        }

        PluginData plugin{};
        plugin.m_filename = line;

        switch (pluginType)
        {
        case PluginType::kMaster:
            m_masterFiles.emplace(plugin.m_filename, standardId);
            [[fallthrough]];
        case PluginType::kStandard:
            plugin.m_standardId = standardId++;
            plugin.m_isLite = false;
            break;
        case PluginType::kLite:
            plugin.m_liteId = liteId++;
            plugin.m_isLite = true;
            break;
        case PluginType::kInvalid: break;
        }

        m_loadOrder.push_back(plugin);
    }

    if (loadOrderFile.bad())
    {
        spdlog::warn("Failed while reading loadorder.txt at '{}'", loadOrderPath.string());
        m_loadOrder.clear();
        m_masterFiles.clear();
        return false;
    }

    return true;
}

UniquePtr<RecordCollection> ESLoader::LoadFiles()
{
    auto recordCollection = MakeUnique<RecordCollection>();

    for (PluginData& plugin : m_loadOrder)
    {
        fs::path pluginPath = GetPath(plugin.m_filename);
        if (pluginPath.empty())
        {
            spdlog::warn("Path to plugin file not found: {}", plugin.m_filename);
            continue;
        }

        TESFile pluginFile(m_masterFiles);
        if (plugin.IsLite())
            pluginFile.Setup(plugin.m_liteId);
        else
            pluginFile.Setup(plugin.m_standardId);

        bool loadResult = pluginFile.LoadFile(pluginPath);

        if (!loadResult)
            continue;

        pluginFile.IndexRecords(*recordCollection);
    }

    return recordCollection;
}

fs::path ESLoader::GetPath(const String& acFilename) const
{
    // loadorder.txt contains plugin filenames, not paths. Reject path syntax so
    // a malformed entry cannot make the loader read outside Data, and resolve
    // the exact path directly instead of depending on directory iteration order.
    if (!IsSafePluginFilename(acFilename))
        return {};

    const fs::path pluginPath = m_directory / fs::path(acFilename);
    std::error_code error;
    if (fs::is_regular_file(pluginPath, error))
        return pluginPath;

    return fs::path();
}

} // namespace ESLoader
