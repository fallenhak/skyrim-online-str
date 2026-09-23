#pragma once

struct PresentationAuthorityPolicy final
{
    [[nodiscard]] static constexpr bool CanRelayScriptAnimation(
        const bool aEntityExists,
        const bool aHasFormId,
        const bool aHasCell,
        const bool aIsNpcCharacter,
        const bool aIsObject,
        const bool aSenderInRange) noexcept
    {
        return aEntityExists && aHasFormId && aHasCell && (aIsNpcCharacter || aIsObject) && aSenderInRange;
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
