#pragma once

#include <Services/RenewableEncounterRegistry.h>

#include <cstddef>
#include <cstdint>
#include <istream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

/**
 * Loads renewable encounters from the server's own configuration (roadmap W12).
 * One directive per line; `#` starts a comment. Form ids are hexadecimal
 * load-order form ids (optional `0x`), groups and cooldowns are decimal:
 *
 *   encounter <cell> <group> [cooldown=<seconds>]
 *   cell      <cell> <group> <extra cell>      # extends the occupancy scope
 *   slot      <cell> <group> <placed ref>      # one spawn slot
 *
 * An encounter must be declared before its cells and slots. A bad line is
 * reported with its line number and skipped; the rest still loads.
 */
struct RenewableEncounterConfigResult final
{
    std::size_t Encounters{};
    std::size_t Cells{};
    std::size_t Slots{};
    std::vector<std::string> Errors;
};

namespace RenewableEncounterConfigDetail
{
[[nodiscard]] inline bool ParseUnsigned(std::string aText, const int aBase, const std::uint64_t aMax, std::uint64_t& aValue)
{
    if (aBase == 16 && aText.size() > 2 && aText[0] == '0' && (aText[1] == 'x' || aText[1] == 'X'))
        aText.erase(0, 2);

    if (aText.empty() || aText.size() > 20)
        return false;

    std::uint64_t value = 0;
    for (const char c : aText)
    {
        std::uint64_t digit;
        if (c >= '0' && c <= '9')
            digit = static_cast<std::uint64_t>(c - '0');
        else if (aBase == 16 && c >= 'a' && c <= 'f')
            digit = static_cast<std::uint64_t>(c - 'a' + 10);
        else if (aBase == 16 && c >= 'A' && c <= 'F')
            digit = static_cast<std::uint64_t>(c - 'A' + 10);
        else
            return false;

        if (value > (aMax - digit) / static_cast<std::uint64_t>(aBase))
            return false;

        value = value * static_cast<std::uint64_t>(aBase) + digit;
    }

    aValue = value;
    return true;
}

[[nodiscard]] inline bool ParseFormId(const std::string& acText, std::uint32_t& aValue)
{
    std::uint64_t value = 0;
    if (!ParseUnsigned(acText, 16, std::numeric_limits<std::uint32_t>::max(), value) || value == 0)
        return false;

    aValue = static_cast<std::uint32_t>(value);
    return true;
}

[[nodiscard]] inline bool ParseGroup(const std::string& acText, std::uint32_t& aValue)
{
    std::uint64_t value = 0;
    if (!ParseUnsigned(acText, 10, std::numeric_limits<std::uint32_t>::max(), value))
        return false;

    aValue = static_cast<std::uint32_t>(value);
    return true;
}

// Returns an empty string on success, otherwise the reason the line was skipped.
[[nodiscard]] inline std::string ApplyLine(const std::vector<std::string>& acTokens, RenewableEncounterRegistry& aRegistry, RenewableEncounterConfigResult& aResult)
{
    const auto& directive = acTokens[0];
    if (directive != "encounter" && directive != "cell" && directive != "slot")
        return "unknown directive '" + directive + "'";

    if (acTokens.size() < 3)
        return "missing encounter cell or group";

    RenewableEncounterId id{};
    if (!ParseFormId(acTokens[1], id.CellFormId))
        return "invalid encounter cell '" + acTokens[1] + "'";
    if (!ParseGroup(acTokens[2], id.GroupIndex))
        return "invalid group '" + acTokens[2] + "'";

    if (directive == "encounter")
    {
        if (acTokens.size() > 4)
            return "too many fields";

        RenewableEncounterPolicy policy{};
        if (acTokens.size() == 4)
        {
            const std::string prefix = "cooldown=";
            std::uint64_t cooldown = 0;
            if (acTokens[3].rfind(prefix, 0) != 0 || !ParseUnsigned(acTokens[3].substr(prefix.size()), 10, std::numeric_limits<std::uint64_t>::max(), cooldown))
                return "invalid option '" + acTokens[3] + "'";
            policy.ResetCooldownTicks = cooldown;
        }

        if (!aRegistry.AddEncounter(id, policy))
            return "encounter already declared";
        ++aResult.Encounters;
        return {};
    }

    if (acTokens.size() != 4)
        return acTokens.size() < 4 ? "missing form id" : "too many fields";

    std::uint32_t formId = 0;
    if (!ParseFormId(acTokens[3], formId))
        return "invalid form id '" + acTokens[3] + "'";

    if (!aRegistry.Find(id))
        return "encounter not declared";

    if (directive == "cell")
    {
        if (!aRegistry.AddEncounterCell(id, formId))
            return "cell already in the encounter";
        ++aResult.Cells;
        return {};
    }

    if (!aRegistry.AddSlot(id, SpawnSlotId{formId}))
        return "slot already used by an encounter";
    ++aResult.Slots;
    return {};
}
} // namespace RenewableEncounterConfigDetail

[[nodiscard]] inline RenewableEncounterConfigResult LoadRenewableEncounterConfig(std::istream& aStream, RenewableEncounterRegistry& aRegistry)
{
    RenewableEncounterConfigResult result;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(aStream, line))
    {
        ++lineNumber;
        if (const auto comment = line.find('#'); comment != std::string::npos)
            line.erase(comment);

        std::istringstream fields(line);
        std::vector<std::string> tokens;
        for (std::string token; fields >> token;)
            tokens.push_back(token);

        if (tokens.empty())
            continue;

        if (auto error = RenewableEncounterConfigDetail::ApplyLine(tokens, aRegistry, result); !error.empty())
            result.Errors.push_back("line " + std::to_string(lineNumber) + ": " + error);
    }

    return result;
}
