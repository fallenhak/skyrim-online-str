#pragma once

#include <cstdint>

struct AuthorityChangedEvent
{
    bool HasLocalActorAuthority{false};
    bool HasLocalWorldAuthority{false};
    bool HasWorldAuthoritySource{false};
    uint32_t WorldAuthorityPlayerId{0};
};
