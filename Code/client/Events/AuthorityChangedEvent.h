#pragma once

#include <cstdint>

struct AuthorityChangedEvent
{
    bool HasLocalActorAuthority{false};
    bool HasLocalWorldAuthority{false};
    bool HasWorldAuthorityGroup{false};
    uint32_t WorldAuthorityPlayerId{0};
};
