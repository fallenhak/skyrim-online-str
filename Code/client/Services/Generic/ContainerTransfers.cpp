#include <Services/ContainerTransfers.h>

#include <World.h>
#include <Components.h>
#include <Games/References.h>

#include <algorithm>
#include <limits>

namespace ContainerTransfers
{
std::optional<uint32_t> GetSyncedContainerServerId(const TESObjectREFR* apReference) noexcept
{
    if (!apReference || !apReference->baseForm || apReference->baseForm->formType != FormType::Container)
        return std::nullopt;

    const auto view = World::Get().view<FormIdComponent, ObjectComponent>();
    for (const auto entity : view)
    {
        if (view.get<FormIdComponent>(entity).Id == apReference->formID)
            return view.get<ObjectComponent>(entity).Id;
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
