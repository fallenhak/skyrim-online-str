#pragma once

#include <Services/RenewableEncounterRegistry.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

/**
 * Binds actors the server creates for placed references to encounter slots
 * (roadmap W13). In vanilla Skyrim the client that loads a cell spawns its
 * placed actors itself and asks the server to assign them; the server then
 * creates the entity and its lifecycle generation. The encounter only has to
 * recognise the placed reference as one of its slots and bind the new
 * incarnation to the current epoch; ClaimSpawn/CompleteSpawn stay for a
 * server-driven spawner.
 */
enum class PlacedActorBindResult : std::uint8_t
{
    NotEncounterSlot,
    Bound,
    // A corpse reloaded into a cleared encounter; it is not a new incarnation.
    ArrivedDead,
    Rejected
};

[[nodiscard]] inline PlacedActorBindResult BindPlacedActor(
    RenewableEncounterRegistry& aRegistry, const std::uint32_t aPlacedRefFormId, const EncounterIncarnation aIncarnation, const bool aIsDead)
{
    const SpawnSlotId slot{aPlacedRefFormId};
    if (!slot.IsValid())
        return PlacedActorBindResult::NotEncounterSlot;

    const auto encounterId = aRegistry.FindEncounter(slot);
    if (!encounterId)
        return PlacedActorBindResult::NotEncounterSlot;

    if (aIsDead)
        return PlacedActorBindResult::ArrivedDead;

    const auto* pEncounter = aRegistry.Find(*encounterId);
    if (!pEncounter || !aRegistry.BindIncarnation(*encounterId, slot, aIncarnation, pEncounter->GetEpoch()))
        return PlacedActorBindResult::Rejected;

    return PlacedActorBindResult::Bound;
}

/**
 * Called when the server removes an actor. A living incarnation frees its
 * slot; a dead one keeps the slot dead. Returns false for an incarnation no
 * encounter currently holds.
 */
[[nodiscard]] inline bool ReleasePlacedActor(RenewableEncounterRegistry& aRegistry, const EncounterIncarnation aIncarnation)
{
    const auto result = aRegistry.ReleaseIncarnation(aIncarnation);
    return result == RenewableEncounterState::ReleaseResult::Released || result == RenewableEncounterState::ReleaseResult::AlreadyDead;
}

/**
 * Returns the server ids of bound actors whose incarnation a reset retired
 * (roadmap W14), sorted. The server removes them so an actor from an old epoch
 * never outlives its slot; current and unknown incarnations are left alone.
 */
[[nodiscard]] inline std::vector<std::uint32_t> CollectStaleBoundActors(
    const RenewableEncounterRegistry& acRegistry, const std::unordered_map<std::uint32_t, EncounterIncarnation>& acBoundByServerId)
{
    std::vector<std::uint32_t> stale;
    for (const auto& [serverId, incarnation] : acBoundByServerId)
    {
        if (acRegistry.GetIncarnationStatus(incarnation) == RenewableEncounterRegistry::IncarnationStatus::Stale)
            stale.push_back(serverId);
    }
    std::sort(stale.begin(), stale.end());
    return stale;
}
