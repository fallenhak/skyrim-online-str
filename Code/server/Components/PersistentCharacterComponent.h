#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Persistence/CharacterRecord.h>

struct PersistentCharacterComponent final
{
    Persistence::CharacterId CharacterId{};
    Persistence::OwnerProfileId OwnerProfileId;
};
