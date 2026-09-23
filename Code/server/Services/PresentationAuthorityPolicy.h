#pragma once

struct PresentationAuthorityPolicy final
{
    [[nodiscard]] static constexpr bool CanRelayScriptAnimation(
        const bool aEntityExists,
        const bool aHasFormId,
        const bool aHasCell,
        const bool aIsNpcCharacter,
        const bool aSenderInRange) noexcept
    {
        // ObjectService has no authoritative static-reference identity or
        // location source. Client-discovered objects must not be animation
        // relay sources until one exists.
        return aEntityExists && aHasFormId && aHasCell && aIsNpcCharacter && aSenderInRange;
    }

    [[nodiscard]] static constexpr bool CanRelayNpcPresentation(
        const bool aEntityExists,
        const bool aIsNpcCharacter,
        const bool aHasFormId,
        const bool aHasCell,
        const bool aSenderInRange) noexcept
    {
        return aEntityExists && aIsNpcCharacter && aHasFormId && aHasCell && aSenderInRange;
    }
};
