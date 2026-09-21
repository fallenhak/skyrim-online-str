#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <atomic>
#include <cstdint>
#include <limits>

/**
 * @brief Server-owned identity for one canonical actor incarnation.
 *
 * EnTT entity values may be reused after an entity is destroyed. This
 * generation is allocated independently of the EnTT identifier and is never
 * wrapped back to an earlier value. A zero generation is reserved for an
 * exhausted allocator and is never a valid actor incarnation.
 */
struct ActorLifecycleComponent final
{
    using Generation = std::uint64_t;

    static constexpr Generation kInvalidGeneration = 0;

    ActorLifecycleComponent() noexcept
        : LifecycleGeneration(AllocateGeneration())
    {
    }

    explicit ActorLifecycleComponent(const Generation aGeneration) noexcept
        : LifecycleGeneration(aGeneration)
    {
    }

    [[nodiscard]] bool IsValid() const noexcept
    {
        return LifecycleGeneration != kInvalidGeneration;
    }

    [[nodiscard]] Generation GetGeneration() const noexcept
    {
        return LifecycleGeneration;
    }

    Generation LifecycleGeneration{};

private:
    [[nodiscard]] static Generation AllocateGeneration() noexcept
    {
        Generation generation = s_nextGeneration.load(std::memory_order_relaxed);
        for (;;)
        {
            if (generation == kInvalidGeneration)
                return kInvalidGeneration;

            const Generation nextGeneration = generation == std::numeric_limits<Generation>::max() ? kInvalidGeneration : generation + 1;
            if (s_nextGeneration.compare_exchange_weak(
                    generation, nextGeneration, std::memory_order_relaxed, std::memory_order_relaxed))
                return generation;
        }
    }

    inline static std::atomic<Generation> s_nextGeneration{1};
};
