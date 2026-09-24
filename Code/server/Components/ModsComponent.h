#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <cstdint>
#include <TiltedCore/Stl.hpp>

namespace ESLoader
{
struct PluginData;
}

struct GameId;

struct ModsComponent
{
    struct Entry
    {
        uint32_t id;
        uint32_t refCount;
    };

    // Resolves a network GameId's server-assigned ModId and BaseId into the
    // ESLoader form identity selected by the server's load order.
    bool ResolveServerFormId(const GameId& acNetworkId, uint32_t& aResolvedFormId) const noexcept;

    uint32_t AddStandard(const TiltedPhoques::String& acpFilename) noexcept;
    uint32_t AddLite(const TiltedPhoques::String& acpFilename) noexcept;

    void AddServerMod(const ESLoader::PluginData& acData);

    const auto& GetStandardMods() const noexcept { return m_standardMods; }
    const auto& GetLiteMods() const noexcept { return m_liteMods; }
    const auto& GetServerMods() const noexcept { return m_serverMods; }

    bool IsInstalled(const TiltedPhoques::String& acpFileName) const noexcept;

    using TModList = TiltedPhoques::Map<TiltedPhoques::String, Entry>;

private:
    struct NetworkModIdentity
    {
        TiltedPhoques::String Filename;
        bool IsLite{};
    };

    struct ServerPluginIdentity
    {
        uint16_t LoadOrderId{};
        bool IsLite{};
    };

    uint32_t m_seed = 0;
    // Mappings of ids owned by the server
    TModList m_standardMods;
    TModList m_liteMods;

    // List of mods installed on the server.
    TModList m_serverMods;

    // The network id is assigned from the client mod list during authentication;
    // these maps connect it to authoritative server plugin metadata without
    // trusting a client-supplied form prefix.
    TiltedPhoques::Map<uint32_t, NetworkModIdentity> m_networkModIdentities;
    TiltedPhoques::Map<TiltedPhoques::String, ServerPluginIdentity> m_serverPluginIdentities;
};
