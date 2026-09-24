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

    /**
     * @brief Start a fresh server-owned actor lifecycle after respawn.
     *
     * The current generation is preserved if the process-wide allocator is
     * exhausted, so callers can reject the respawn without leaving an actor
     * with a partially reset lifecycle.
     */
    [[nodiscard]] bool TryStartNewIncarnation() noexcept
    {
        if (!IsValid())
            return false;

        const auto generation = AllocateGeneration();
        if (generation == kInvalidGeneration)
            return false;

        LifecycleGeneration = generation;
        AcceptedCanonicalCreatureDeathGeneration = 0;
        return true;
    }

    [[nodiscard]] bool TryMarkCanonicalCreatureDeathAccepted() noexcept
    {
        if (!IsValid() || AcceptedCanonicalCreatureDeathGeneration == LifecycleGeneration)
            return false;

        AcceptedCanonicalCreatureDeathGeneration = LifecycleGeneration;
        return true;
    }

    Generation LifecycleGeneration{};
    // Zero means no canonical Creature death event has been accepted for
    // this incarnation. Lifecycle generations are never zero or reused.
    Generation AcceptedCanonicalCreatureDeathGeneration{};

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
