
#define TP_INTERNAL_COMPONENTS_GUARD
#include <Components/ModsComponent.h>
#undef TP_INTERNAL_COMPONENTS_GUARD

#include <Structs/GameId.h>
#include <es_loader/ESLoader.h>

uint32_t ModsComponent::AddStandard(const String& acpFilename) noexcept
{
    const auto itor = m_standardMods.find(acpFilename);
    if (itor != std::end(m_standardMods))
    {
        itor.value().refCount++;
        return itor->second.id;
    }

    const auto id = m_seed++;
    m_standardMods.emplace(acpFilename, Entry{id, 1});
    m_networkModIdentities.emplace(id, NetworkModIdentity{acpFilename, false});

    return id;
}

uint32_t ModsComponent::AddLite(const String& acpFilename) noexcept
{
    const auto itor = m_liteMods.find(acpFilename);
    if (itor != std::end(m_liteMods))
    {
        itor.value().refCount++;
        return itor->second.id;
    }

    const auto id = m_seed++;
    m_liteMods.emplace(acpFilename, Entry{id, 1});
    m_networkModIdentities.emplace(id, NetworkModIdentity{acpFilename, true});

    return id;
}

void ModsComponent::AddServerMod(const ESLoader::PluginData& acData)
{
    const uint16_t loadOrderId = acData.IsLite() ? acData.m_liteId : acData.m_standardId;
    const uint16_t maximumId = acData.IsLite() ? ESLoader::kMaxLitePluginId : ESLoader::kMaxStandardPluginId;
    if (loadOrderId > maximumId)
    {
        spdlog::warn("Ignoring server plugin {} with out-of-range load-order ID {}", acData.m_filename, loadOrderId);
        return;
    }

    m_serverPluginIdentities[acData.m_filename] = ServerPluginIdentity{
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

    const auto serverIt = m_serverPluginIdentities.find(networkIt->second.Filename);
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
    auto it = std::find_if(m_serverMods.begin(), m_serverMods.end(), [&](const TModList::value_type& aEntry) { return aEntry.first == acpFilename; });

    return it != m_serverMods.end();
}
