#pragma once

#include <TiltedCore/Stl.hpp>

#include <cstddef>
#include <cstdint>

class PopulationDisableTracker final
{
public:
    [[nodiscard]] bool OwnDisable(const std::uint32_t aFormId) noexcept
    {
        if (aFormId == 0x14)
            return false;

        return m_disabledByPopulationPolicy.insert(aFormId).second;
    }

    [[nodiscard]] bool OwnsDisable(const std::uint32_t aFormId) const noexcept
    {
        return m_disabledByPopulationPolicy.find(aFormId) != m_disabledByPopulationPolicy.end();
    }

    [[nodiscard]] TiltedPhoques::Set<std::uint32_t> DrainOwnedDisables() noexcept
    {
        TiltedPhoques::Set<std::uint32_t> drained;
        drained.swap(m_disabledByPopulationPolicy);
        return drained;
    }

    [[nodiscard]] std::size_t Size() const noexcept
    {
        return m_disabledByPopulationPolicy.size();
    }

private:
    TiltedPhoques::Set<std::uint32_t> m_disabledByPopulationPolicy{};
};
