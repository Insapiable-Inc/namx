#pragma once

// Names every app source file uses. They are imported into namx once, here, so our code
// writes `string` and `vector` instead of `std::string` and `std::vector`
// (CPP_GUIDELINES.md → Namespaces). The list grows deliberately.

#include <array>
#include <atomic>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace namx
{

using std::array, std::atomic, std::expected, std::make_unique, std::size_t, std::span, std::string,
    std::string_view, std::unique_ptr, std::vector;

// The UTF-8 string the UI hands the DSP as a path. std::filesystem::path(string) would
// read it in the Windows native code page and miss non-ASCII names (audit 2026-10-04
// item 11); the char8_t constructor is UTF-8 on every platform.
inline std::filesystem::path utf8_path(string_view utf8) {
    return std::filesystem::path(
        std::u8string_view(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

} // namespace namx
