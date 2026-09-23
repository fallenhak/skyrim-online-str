#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

/**
 * Stable logical identity of a renewable encounter (a dungeon or a group of
 * placed creatures that clears and resets together).
 *
 * It must survive server restarts, so it is built from load-order independent
 * data resolved by the server (e.g. the owning cell's resolved form id) and a
 * designer-chosen group index, never from EnTT or network ids.
 */
struct RenewableEncounterId final
{
    std::uint32_t CellFormId{};
    std::uint32_t GroupIndex{};

    [[nodiscard]] constexpr bool IsValid() const noexcept { return CellFormId != 0; }

    friend constexpr bool operator<(const RenewableEncounterId& acLeft, const RenewableEncounterId& acRight) noexcept
    {
        return std::tie(acLeft.CellFormId, acLeft.GroupIndex) < std::tie(acRight.CellFormId, acRight.GroupIndex);
    }

    friend constexpr bool operator==(const RenewableEncounterId& acLeft, const RenewableEncounterId& acRight) noexcept
    {
        return acLeft.CellFormId == acRight.CellFormId && acLeft.GroupIndex == acRight.GroupIndex;
    }
};

/**
 * Stable logical spawn point inside an encounter: the resolved form id of the
 * placed actor reference. The creature standing in the slot changes on every
 * reset; the slot does not.
 */
struct SpawnSlotId final
{
    std::uint32_t PlacedRefFormId{};

    [[nodiscard]] constexpr bool IsValid() const noexcept { return PlacedRefFormId != 0; }

    friend constexpr bool operator<(const SpawnSlotId& acLeft, const SpawnSlotId& acRight) noexcept { return acLeft.PlacedRefFormId < acRight.PlacedRefFormId; }

    friend constexpr bool operator==(const SpawnSlotId& acLeft, const SpawnSlotId& acRight) noexcept { return acLeft.PlacedRefFormId == acRight.PlacedRefFormId; }
};

/**
 * One server-side actor incarnation (server id + lifecycle generation), the
 * same identity shape CombatContributionLedger uses for its targets.
 */
struct EncounterIncarnation final
{
    std::uint32_t ServerId{};
    std::uint64_t LifecycleGeneration{};

    [[nodiscard]] constexpr bool IsValid() const noexcept { return LifecycleGeneration != 0; }

    friend constexpr bool operator<(const EncounterIncarnation& acLeft, const EncounterIncarnation& acRight) noexcept
    {
        return std::tie(acLeft.ServerId, acLeft.LifecycleGeneration) < std::tie(acRight.ServerId, acRight.LifecycleGeneration);
    }

    friend constexpr bool operator==(const EncounterIncarnation& acLeft, const EncounterIncarnation& acRight) noexcept
    {
        return acLeft.ServerId == acRight.ServerId && acLeft.LifecycleGeneration == acRight.LifecycleGeneration;
    }
};

struct RenewableEncounterPolicy final
{
    std::uint64_t ResetCooldownTicks{30 * 60};
};

/**
 * Server-owned membership and reset state for one renewable encounter
 * (roadmap W01, W02, W04).
 *
 * The class intentionally has no packet, Player or combat API. Deaths enter
 * only through RecordVerifiedDeath, which a future handler calls after the
 * combat lane has verified the kill (W03); a client claim must never reach it
 * directly. Occupancy (W05) and retired incarnations (W06) are tracked by
 * RenewableEncounterRegistry, which owns every mutation in production.
 *
 * The epoch increases on every reset. A spawn must carry the epoch it was
 * requested in, so a spawn that completes after a reset cannot refill a slot of
 * the new cycle.
 */
class RenewableEncounterState final
{
public:
    enum class SlotStatus : std::uint8_t
    {
        Unknown,
        Unbound,
        Alive,
        Dead
    };

    enum class DeathResult : std::uint8_t
    {
        Recorded,
        AlreadyDead,
        UnknownIncarnation,
        // Only RenewableEncounterRegistry reports this: the incarnation was
        // retired by a reset or release (W06).
        StaleIncarnation
    };

    enum class ReleaseResult : std::uint8_t
    {
        Released,
        AlreadyDead,
        UnknownIncarnation,
        StaleIncarnation
    };

    struct Membership final
    {
        std::size_t Unbound{};
        std::size_t Alive{};
        std::size_t Dead{};
    };

    RenewableEncounterState(const RenewableEncounterId aId, const RenewableEncounterPolicy aPolicy) noexcept
        : m_id(aId)
        , m_policy(aPolicy)
    {
    }

    [[nodiscard]] const RenewableEncounterId& GetId() const noexcept { return m_id; }
    [[nodiscard]] std::size_t GetSlotCount() const noexcept { return m_slots.size(); }
    [[nodiscard]] std::uint64_t GetResetCount() const noexcept { return m_resetCount; }
    [[nodiscard]] std::uint64_t GetEpoch() const noexcept { return m_resetCount; }
    [[nodiscard]] std::optional<std::uint64_t> GetClearedTick() const noexcept { return m_clearedTick; }

    [[nodiscard]] bool AddSlot(const SpawnSlotId aSlot)
    {
        // A cleared encounter must not gain a slot that was never populated.
        if (!m_id.IsValid() || !aSlot.IsValid() || IsCleared())
            return false;

        return m_slots.emplace(aSlot, Slot{}).second;
    }

    [[nodiscard]] bool BindIncarnation(const SpawnSlotId aSlot, const EncounterIncarnation aIncarnation, const std::uint64_t aSpawnEpoch)
    {
        if (aSpawnEpoch != GetEpoch() || !aIncarnation.IsValid() || FindSlot(aIncarnation))
            return false;

        const auto it = m_slots.find(aSlot);
        if (it == m_slots.end() || it->second.Status != SlotStatus::Unbound)
            return false;

        it->second.Incarnation = aIncarnation;
        it->second.Status = SlotStatus::Alive;
        m_slotByIncarnation.emplace(aIncarnation, aSlot);
        return true;
    }

    [[nodiscard]] std::optional<SpawnSlotId> FindSlot(const EncounterIncarnation aIncarnation) const noexcept
    {
        const auto it = m_slotByIncarnation.find(aIncarnation);
        if (it == m_slotByIncarnation.end())
            return std::nullopt;

        return it->second;
    }

    /**
     * Frees the slot of a live incarnation that left the world without dying
     * (despawn, cell unload, lost ownership). A dead incarnation stays dead so
     * unloading a corpse can never undo a verified death.
     */
    [[nodiscard]] ReleaseResult ReleaseIncarnation(const EncounterIncarnation aIncarnation)
    {
        const auto slotId = FindSlot(aIncarnation);
        if (!slotId)
            return ReleaseResult::UnknownIncarnation;

        auto& slot = m_slots.at(*slotId);
        if (slot.Status == SlotStatus::Dead)
            return ReleaseResult::AlreadyDead;

        slot = Slot{};
        m_slotByIncarnation.erase(aIncarnation);
        return ReleaseResult::Released;
    }

    [[nodiscard]] SlotStatus GetSlotStatus(const SpawnSlotId aSlot) const noexcept
    {
        const auto it = m_slots.find(aSlot);
        return it == m_slots.end() ? SlotStatus::Unknown : it->second.Status;
    }

    /**
     * Applies a combat-verified death. Only the incarnation currently bound to
     * a slot counts; deaths of earlier incarnations (late packets after a
     * reset or EnTT id reuse) are rejected.
     */
    [[nodiscard]] DeathResult RecordVerifiedDeath(const EncounterIncarnation aIncarnation, const std::uint64_t aTick)
    {
        const auto slotId = FindSlot(aIncarnation);
        if (!slotId)
            return DeathResult::UnknownIncarnation;

        auto& slot = m_slots.at(*slotId);
        if (slot.Status == SlotStatus::Dead)
            return DeathResult::AlreadyDead;

        slot.Status = SlotStatus::Dead;
        if (AllSlotsDead())
            m_clearedTick = aTick;

        return DeathResult::Recorded;
    }

    [[nodiscard]] bool IsCleared() const noexcept { return m_clearedTick.has_value(); }

    [[nodiscard]] Membership GetMembership() const noexcept
    {
        Membership membership{};
        for (const auto& [slotId, slot] : m_slots)
        {
            if (slot.Status == SlotStatus::Alive)
                ++membership.Alive;
            else if (slot.Status == SlotStatus::Dead)
                ++membership.Dead;
            else
                ++membership.Unbound;
        }

        return membership;
    }

    /**
     * Slots that still need a spawn in the current epoch. A cleared encounter
     * needs none until it resets.
     */
    [[nodiscard]] std::vector<SpawnSlotId> GetUnboundSlots() const
    {
        std::vector<SpawnSlotId> slots;
        if (IsCleared())
            return slots;

        for (const auto& [slotId, slot] : m_slots)
        {
            if (slot.Status == SlotStatus::Unbound)
                slots.push_back(slotId);
        }

        return slots;
    }

    [[nodiscard]] bool IsResetEligible(const std::uint64_t aNowTick) const noexcept
    {
        if (!m_clearedTick || aNowTick < *m_clearedTick)
            return false;

        return aNowTick - *m_clearedTick >= m_policy.ResetCooldownTicks;
    }

    /**
     * Empties every slot so fresh incarnations can be bound. Previously bound
     * incarnations become unknown and can no longer affect the encounter.
     */
    [[nodiscard]] bool TryReset(const std::uint64_t aNowTick)
    {
        if (!IsResetEligible(aNowTick))
            return false;

        for (auto& [slotId, slot] : m_slots)
            slot = Slot{};

        m_slotByIncarnation.clear();
        m_clearedTick.reset();
        ++m_resetCount;
        return true;
    }

private:
    struct Slot final
    {
        EncounterIncarnation Incarnation{};
        SlotStatus Status{SlotStatus::Unbound};
    };

    [[nodiscard]] bool AllSlotsDead() const noexcept
    {
        if (m_slots.empty())
            return false;

        for (const auto& [slotId, slot] : m_slots)
        {
            if (slot.Status != SlotStatus::Dead)
                return false;
        }

        return true;
    }

    RenewableEncounterId m_id;
    RenewableEncounterPolicy m_policy;
    std::map<SpawnSlotId, Slot> m_slots;
    std::map<EncounterIncarnation, SpawnSlotId> m_slotByIncarnation;
    std::optional<std::uint64_t> m_clearedTick;
    std::uint64_t m_resetCount{};
};
