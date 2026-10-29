#include "app/Settings.hpp"

#include "app/util/Files.hpp"
#include "core/json/Json.hpp"

namespace sloptube::app {

Settings Settings::load(const std::filesystem::path& file) {
    Settings settings;
    auto text = readText(file);
    if (!text) {
        return settings;
    }
    auto root = json::parse(*text);
    if (!root) {
        return settings;
    }
    // Absent keys keep their defaults, so settings added later need no migration.
    const auto read = [&](std::string_view pointer, bool& value) {
        if (json::has(*root, pointer)) {
            value = json::boolean(*root, pointer);
        }
    };
    read("/backgroundPlay", settings.backgroundPlay);
    read("/pictureInPicture", settings.pictureInPicture);
    read("/autoplay", settings.autoplay);
    read("/uploadNotifications", settings.uploadNotifications);
    return settings;
}

void Settings::save(const std::filesystem::path& file) const {
    json::Writer out;
    out.beginObject()
        .field("backgroundPlay", backgroundPlay)
        .field("pictureInPicture", pictureInPicture)
        .field("autoplay", autoplay)
        .field("uploadNotifications", uploadNotifications)
        .endObject();
    writeText(file, out.take());
}

} // namespace sloptube::app
