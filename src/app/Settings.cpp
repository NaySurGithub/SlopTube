#include "app/Settings.hpp"

#include "app/util/Files.hpp"
#include "core/json/Json.hpp"

#include <algorithm>

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
    read("/checkUpdates", settings.checkUpdates);
    settings.interfaceSize = static_cast<int>(
        std::clamp<std::int64_t>(json::integer(*root, "/interfaceSize"), 0, kInterfaceSizes - 1));
    const auto language = json::integer(*root, "/language");
    if (language >= 0 && language < kLanguageChoices) {
        settings.language = static_cast<int>(language);
    }
    return settings;
}

void Settings::save(const std::filesystem::path& file) const {
    json::Writer out;
    out.beginObject()
        .field("backgroundPlay", backgroundPlay)
        .field("pictureInPicture", pictureInPicture)
        .field("autoplay", autoplay)
        .field("uploadNotifications", uploadNotifications)
        .field("checkUpdates", checkUpdates)
        .field("interfaceSize", static_cast<std::int64_t>(interfaceSize))
        .field("language", static_cast<std::int64_t>(language))
        .endObject();
    writeText(file, out.take());
}

} // namespace sloptube::app
