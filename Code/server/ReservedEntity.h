#pragma once

#include <entt/entt.hpp>

// Server ids travel as plain integers, and several paths read 0 as "none": a spell's desired
// target, a caster, a respawn lookup. Entity 0 used to be the first character created after a
// restart, and that player could then not be targeted or interacted with (#40, test 5). The
// registry hands out 0 first, so reserving it before anything else keeps 0 meaning "none".
// The entity has no components and is never destroyed, so no view or save ever sees it.
[[nodiscard]] inline entt::entity ReserveNullServerEntity(entt::registry& aRegistry) noexcept
{
    return aRegistry.create();
}
