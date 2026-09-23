

#include "ESLoader.h"
#include <algorithm>
#include <cctype>
#include <cstring>
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

PluginType GetAuthoritativePluginType(const String& acFilename, const fs::path& acPath) noexcept
{
    const auto extensionType = GetPluginType(acFilename);
    const auto headerFlags = TESFile::ReadHeaderFlags(acPath);
    if (!headerFlags)
        return PluginType::kInvalid;

    // The TES4 ESL bit promotes an .esp or .esm into the FE/light namespace.
    // It must not override the filename's established .esl light namespace.
    if ((*headerFlags & Record::FLAGS::kESL) != 0)
        return PluginType::kLite;

    // A readable header never downgrades .esl. For .esp/.esm, the extension
    // retains the standard/master namespace when the promotion bit is absent.

    return extensionType;
}
} // namespace

String ReadZString(Buffer::Reader& aReader) noexcept
{
    String zstring;
    while (!aReader.Eof())
    {
        const char character = *reinterpret_cast<const char*>(aReader.GetDataAtPosition());
        aReader.Advance(1);
        if (character == '\0')
            break;
        zstring.push_back(character);
    }
    return zstring;
}

bool ReadZString(Buffer::Reader& aReader, const size_t aChunkSize, String& aOutput)
{
    constexpr size_t kMaximumPluginStringSize = 4096;
    if (aChunkSize == 0 || aChunkSize > kMaximumPluginStringSize)
        return false;

    const auto* const pString = reinterpret_cast<const char*>(aReader.GetDataAtPosition());
    const auto* const pTerminator = static_cast<const char*>(std::memchr(pString, '\0', aChunkSize));
    if (pTerminator == nullptr)
        return false;

    aOutput.assign(pString, static_cast<size_t>(pTerminator - pString));
    return true;
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
            spdlog::warn("Actor population record loading unavailable: ESLoader could not establish valid load-order metadata from '{}'", m_directory.string());
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

    uint32_t standardId = 0;
    uint32_t liteId = 0;
    std::set<String> seenFilenames;
    bool firstLine = true;
    String line;

    while (std::getline(loadOrderFile, line))
    {
        line = NormalizeLoadOrderLine(std::move(line), firstLine);
        firstLine = false;

        if (line.empty() || line.front() == '#')
            continue;

        String filenameKey;
        if (!GetPluginFilenameKey(line, filenameKey))
        {
            spdlog::warn("Ignoring unsafe plugin entry in loadorder.txt");
            continue;
        }

        const auto extensionType = GetPluginType(line);
        if (extensionType == PluginType::kInvalid)
        {
            spdlog::warn("Ignoring unrecognized plugin entry in loadorder.txt: {}", line);
            continue;
        }

        // Reading this fixed-size TES4 header establishes server-owned plugin
        // namespace metadata; full record indexing remains opt-in below.
        const auto pluginPath = GetPath(line);
        const auto pluginType = pluginPath.empty() ? extensionType : GetAuthoritativePluginType(line, pluginPath);
        if (pluginType == PluginType::kInvalid)
        {
            spdlog::warn("Ignoring plugin with invalid TES4 header: {}", line);
            continue;
        }

        if (!seenFilenames.emplace(std::move(filenameKey)).second)
        {
            spdlog::warn("Ignoring duplicate plugin entry in loadorder.txt: {}", line);
            continue;
        }

        PluginData plugin{};
        plugin.m_filename = line;

        switch (pluginType)
        {
        case PluginType::kMaster:
        case PluginType::kStandard:
            if (standardId > kMaxStandardPluginId)
            {
                spdlog::error(
                    "Too many standard plugins in loadorder.txt: '{}' exceeds the maximum load-order ID {}",
                    plugin.m_filename,
                    kMaxStandardPluginId);
                m_loadOrder.clear();
                m_masterFiles.clear();
                return false;
            }

            plugin.m_standardId = static_cast<uint8_t>(standardId++);
            plugin.m_isLite = false;
            break;
        case PluginType::kLite:
            if (liteId > kMaxLitePluginId)
            {
                spdlog::error(
                    "Too many light plugins in loadorder.txt: '{}' exceeds the maximum load-order ID {}",
                    plugin.m_filename,
                    kMaxLitePluginId);
                m_loadOrder.clear();
                m_masterFiles.clear();
                return false;
            }

            plugin.m_liteId = static_cast<uint16_t>(liteId++);
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
        if (!pluginPath.empty())
        {
            TESFile pluginFile(m_masterFiles);
            const bool setupResult = plugin.IsLite() ? pluginFile.Setup(static_cast<uint16_t>(plugin.m_liteId))
                                                     : pluginFile.Setup(static_cast<uint8_t>(plugin.m_standardId));
            if (setupResult && pluginFile.LoadFile(pluginPath))
                pluginFile.IndexRecords(*recordCollection);
        }
        else
        {
            spdlog::warn("Path to plugin file not found: {}", plugin.m_filename);
        }

        // The resolver for the current plugin saw only earlier prefixes. Add
        // this namespace now so later plugins can refer to it. If records are
        // absent, RecordCollection lookups still leave the target unresolved.
        const uint32_t formIdPrefix = plugin.IsLite()
                                          ? 0xFE000000u | (static_cast<uint32_t>(plugin.m_liteId) << 12)
                                          : static_cast<uint32_t>(plugin.m_standardId) << 24;
        String filenameKey;
        if (GetPluginFilenameKey(plugin.m_filename, filenameKey))
            m_masterFiles.emplace(std::move(filenameKey), formIdPrefix);
    }

    return recordCollection;
}

fs::path ESLoader::GetPath(const String& acFilename) const
{
    // loadorder.txt contains plugin filenames, not paths. Reject path syntax so
    // a malformed entry cannot make the loader read outside Data, and resolve
    // the exact path directly instead of depending on directory iteration order.
    String filenameKey;
    if (!GetPluginFilenameKey(acFilename, filenameKey))
        return {};

    const fs::path pluginPath = m_directory / fs::path(acFilename);
    std::error_code error;
    const auto status = fs::symlink_status(pluginPath, error);
    if (error && error != std::errc::no_such_file_or_directory)
    {
        // Other lookup errors leave file existence unknown. Retain the path so
        // header reading fails closed instead of guessing a namespace.
        return pluginPath;
    }

    // Prefer the exact spelling when it exists, then resolve a case-only
    // mismatch in the Data directory. A path never comes from the load-order
    // entry, so this scan cannot escape the plugin directory.
    if (!error && status.type() != fs::file_type::not_found)
        return pluginPath;

    std::error_code directoryError;
    fs::directory_iterator it(m_directory, directoryError);
    if (directoryError)
        return {};

    const fs::directory_iterator end;
    fs::path match;
    while (it != end)
    {
        String entryKey;
        const auto entryFilenameValue = it->path().filename().string();
        const String entryFilename(entryFilenameValue.c_str());
        if (GetPluginFilenameKey(entryFilename, entryKey) && entryKey == filenameKey)
        {
            // Distinct names that compare equal by case are ambiguous. Do not
            // select one based on filesystem iteration order.
            if (!match.empty())
                return {};
            match = it->path();
        }

        it.increment(directoryError);
        if (directoryError)
            return {};
    }

    return match;
}

} // namespace ESLoader
