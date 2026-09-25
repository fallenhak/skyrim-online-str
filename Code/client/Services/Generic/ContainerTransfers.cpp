#include <Services/ContainerTransfers.h>

#include <World.h>
#include <Components.h>
#include <Games/References.h>
#include <Actor.h>
#include <PlayerCharacter.h>

#include <algorithm>
#include <limits>

namespace ContainerTransfers
{
namespace
{
std::optional<Target> GetCorpseTarget(const Actor* apActor) noexcept
{
    if (apActor == PlayerCharacter::Get() || !apActor->IsDead())
        return std::nullopt;

    auto& world = World::Get();
    const auto view = world.view<FormIdComponent>();
    for (const auto entity : view)
    {
        if (view.get<FormIdComponent>(entity).Id != apActor->formID)
            continue;

        if (world.all_of<PlayerComponent>(entity))
            return std::nullopt;
        if (const auto* pLocal = world.try_get<LocalComponent>(entity))
            return Target{pLocal->Id, TargetKind::kCorpse};
        if (const auto* pRemote = world.try_get<RemoteComponent>(entity))
            return Target{pRemote->Id, TargetKind::kCorpse};
        return std::nullopt;
    }

    return std::nullopt;
}
} // namespace

std::optional<Target> GetSyncedTarget(const TESObjectREFR* apReference) noexcept
{
    if (!apReference)
        return std::nullopt;

    if (const auto* pActor = Cast<Actor>(apReference))
        return GetCorpseTarget(pActor);

    if (!apReference->baseForm || apReference->baseForm->formType != FormType::Container)
        return std::nullopt;

    const auto view = World::Get().view<FormIdComponent, ObjectComponent>();
    for (const auto entity : view)
    {
        if (view.get<FormIdComponent>(entity).Id == apReference->formID)
            return Target{view.get<ObjectComponent>(entity).Id, TargetKind::kObject};
    }

    return std::nullopt;
}

int32_t CountOf(const TESObjectREFR* apReference, const Inventory::Entry& acItem) noexcept
{
    int64_t count = 0;
    for (const auto& entry : apReference->GetInventory().Entries)
    {
        if (entry.CanBeMerged(acItem))
            count += entry.Count;
    }
    return static_cast<int32_t>(std::clamp<int64_t>(count, 0, std::numeric_limits<int32_t>::max()));
}
} // namespace ContainerTransfers
