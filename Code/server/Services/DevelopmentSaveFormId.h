#pragma once

#include <Structs/GameId.h>
#include <Structs/Mods.h>

#include <algorithm>
#include <cstdint>
#include <iterator>

/**
 * Maps a local form ID from AuthenticationRequest to the server-assigned mod ID.
 * Only the opt-in development save bootstrap uses this client-provided data.
 */
[[nodiscard]] inline GameId ResolveDevelopmentSaveFormId(
    const std::uint32_t aLocalFormId, const Mods& acUserMods, const TiltedPhoques::Vector<std::uint16_t>& acServerModIds) noexcept
{
    if (aLocalFormId == 0)
        return {};

    const bool isLite = (aLocalFormId >> 24) == 0xFE;
    const std::uint32_t localModId = isLite ? ((aLocalFormId & 0x00FFF000u) >> 12) : (aLocalFormId >> 24);
    const auto mod = std::find_if(
        acUserMods.ModList.begin(), acUserMods.ModList.end(),
        [isLite, localModId](const Mods::Entry& acEntry)
        {
            return acEntry.IsLite == isLite && acEntry.Id == localModId;
        });
    if (mod == acUserMods.ModList.end())
        return {};

    const auto modIndex = static_cast<std::size_t>(std::distance(acUserMods.ModList.begin(), mod));
    if (modIndex >= acServerModIds.size())
        return {};

    const std::uint32_t baseId = isLite ? (aLocalFormId & 0x00000FFFu) : (aLocalFormId & 0x00FFFFFFu);
    return GameId(acServerModIds[modIndex], baseId);
}
