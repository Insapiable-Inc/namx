#pragma once

// Names every file in gui/ uses. They are imported into namx once, here, so our code
// writes `string` and `vector` instead of `std::string` and `std::vector`
// (CPP_GUIDELINES.md → Namespaces). The list grows deliberately.

#include <array>
#include <atomic>
#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace namx
{

using std::array, std::atomic, std::expected, std::make_unique, std::size_t, std::span, std::string,
    std::string_view, std::unique_ptr, std::vector;

} // namespace namx
