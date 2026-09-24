#pragma once

#include <string>
#include <string_view>

namespace CharacterNamePolicy
{
[[nodiscard]] bool IsValid(std::string_view acName) noexcept;
[[nodiscard]] std::string MakeUniquenessKey(std::string_view acName);
} // namespace CharacterNamePolicy
