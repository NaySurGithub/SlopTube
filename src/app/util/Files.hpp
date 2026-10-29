#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace sloptube::app {

// The whole file, or nullopt when it cannot be opened.
[[nodiscard]] std::optional<std::string> readText(const std::filesystem::path& path);

// Replaces the file atomically (a temporary file, then a rename).
bool writeText(const std::filesystem::path& path, std::string_view text);

} // namespace sloptube::app
