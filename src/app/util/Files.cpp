#include "app/util/Files.hpp"

#include <fstream>
#include <iterator>

namespace sloptube::app {

std::optional<std::string> readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), {});
}

bool writeText(const std::filesystem::path& path, std::string_view text) {
    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file) {
            return false;
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    return !error;
}

} // namespace sloptube::app
