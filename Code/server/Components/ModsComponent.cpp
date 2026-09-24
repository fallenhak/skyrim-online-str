
#define TP_INTERNAL_COMPONENTS_GUARD
#include <Components/ModsComponent.h>
#undef TP_INTERNAL_COMPONENTS_GUARD

#include <Structs/GameId.h>
#include <es_loader/ESLoader.h>

#include <utility>

uint32_t ModsComponent::AddStandard(const String& acpFilename) noexcept
{
    String filenameKey;
    const bool hasFilenameKey = ESLoader::GetPluginFilenameKey(acpFilename, filenameKey);
    for (const auto& entry : m_standardMods)
    {
        String existingKey;
        if ((hasFilenameKey && ESLoader::GetPluginFilenameKey(entry.first, existingKey) && existingKey == filenameKey) ||
            (!hasFilenameKey && entry.first == acpFilename))
        {
            auto& duplicate = m_standardMods[entry.first];
            duplicate.refCount++;
            return duplicate.id;
        }
    }

    const auto id = m_seed++;
    m_standardMods.emplace(acpFilename, Entry{id, 1});
    m_networkModIdentities.emplace(id, NetworkModIdentity{std::move(filenameKey), false});

    return id;
}

uint32_t ModsComponent::AddLite(const String& acpFilename) noexcept
{
    String filenameKey;
    const bool hasFilenameKey = ESLoader::GetPluginFilenameKey(acpFilename, filenameKey);
    for (const auto& entry : m_liteMods)
    {
        String existingKey;
        if ((hasFilenameKey && ESLoader::GetPluginFilenameKey(entry.first, existingKey) && existingKey == filenameKey) ||
            (!hasFilenameKey && entry.first == acpFilename))
        {
            auto& duplicate = m_liteMods[entry.first];
            duplicate.refCount++;
            return duplicate.id;
        }
    }

    const auto id = m_seed++;
    m_liteMods.emplace(acpFilename, Entry{id, 1});
    m_networkModIdentities.emplace(id, NetworkModIdentity{std::move(filenameKey), true});

    return id;
}

void ModsComponent::AddServerMod(const ESLoader::PluginData& acData)
{
    String filenameKey;
    if (!ESLoader::GetPluginFilenameKey(acData.m_filename, filenameKey))
    {
        spdlog::warn("Ignoring server plugin with unsafe filename: {}", acData.m_filename);
        return;
    }

    const uint16_t loadOrderId = acData.IsLite() ? acData.m_liteId : acData.m_standardId;
    const uint16_t maximumId = acData.IsLite() ? ESLoader::kMaxLitePluginId : ESLoader::kMaxStandardPluginId;
    if (loadOrderId > maximumId)
    {
        spdlog::warn("Ignoring server plugin {} with out-of-range load-order ID {}", acData.m_filename, loadOrderId);
        return;
    }

    m_serverPluginIdentities[std::move(filenameKey)] = ServerPluginIdentity{
        loadOrderId, acData.IsLite()};

    // Keep the installed-mod entry consistent with the validated namespace ID.
    m_serverMods.emplace(acData.m_filename, Entry{loadOrderId, 1});
}

bool ModsComponent::ResolveServerFormId(const GameId& acNetworkId, uint32_t& aResolvedFormId) const noexcept
{
    aResolvedFormId = 0;

    const auto networkIt = m_networkModIdentities.find(acNetworkId.ModId);
    if (networkIt == m_networkModIdentities.end())
        return false;

    if (networkIt->second.FilenameKey.empty())
        return false;

    const auto serverIt = m_serverPluginIdentities.find(networkIt->second.FilenameKey);
    if (serverIt == m_serverPluginIdentities.end() || serverIt->second.IsLite != networkIt->second.IsLite)
        return false;

    if (serverIt->second.IsLite)
    {
        if (serverIt->second.LoadOrderId > ESLoader::kMaxLitePluginId)
            return false;

        // Light-plugin forms live in the FE namespace and use only the low 12
        // bits of the network BaseId. The server supplies the FE/load-order
        // prefix; high client bits are never authoritative.
        aResolvedFormId = 0xFE000000u | (static_cast<uint32_t>(serverIt->second.LoadOrderId) << 12) | (acNetworkId.BaseId & 0x00000FFFu);
    }
    else
    {
        if (serverIt->second.LoadOrderId > ESLoader::kMaxStandardPluginId)
            return false;

        // Standard-plugin forms use the server's load-order byte and the
        // client-provided record-local 24-bit portion only.
        aResolvedFormId = (static_cast<uint32_t>(serverIt->second.LoadOrderId) << 24) | (acNetworkId.BaseId & 0x00FFFFFFu);
    }

    return true;
}

bool ModsComponent::IsInstalled(const String& acpFilename) const noexcept
{
    String filenameKey;
    return ESLoader::GetPluginFilenameKey(acpFilename, filenameKey) && m_serverPluginIdentities.find(filenameKey) != m_serverPluginIdentities.end();
}
