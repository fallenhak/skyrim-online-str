#include <ReservedEntity.h>

#include <catch2/catch.hpp>

TEST_CASE("Server entity 0 is reserved before any character exists", "[entity_zero]")
{
    entt::registry registry;
    const auto reserved = ReserveNullServerEntity(registry);
    REQUIRE(entt::to_integral(reserved) == 0);

    // The first real entity, e.g. the first player's character after a restart.
    const auto firstCharacter = registry.create();
    REQUIRE(entt::to_integral(firstCharacter) != 0);
}

TEST_CASE("Destroyed entities never recycle into id 0", "[entity_zero]")
{
    entt::registry registry;
    static_cast<void>(ReserveNullServerEntity(registry));

    for (int i = 0; i < 100; ++i)
    {
        const auto entity = registry.create();
        REQUIRE(entt::to_integral(entity) != 0);
        registry.destroy(entity);
    }
}
