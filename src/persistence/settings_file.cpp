// settings_file.cpp — reading and writing the settings file (see
// settings_file.h).
#include "persistence/settings_file.h"

#include "persistence/files.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <fstream>
#include <ostream>

namespace {

// j[key] as a volume, 0 to 1, or `fallback` if it isn't a number.
float volume(const nlohmann::json& j, const char* key, float fallback) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number())
        return fallback;
    return std::clamp(it->get<float>(), 0.0f, 1.0f);
}

}  // namespace

Settings read_settings_file(const std::filesystem::path& path) {
    Settings settings;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return settings;
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object())
        return settings;
    settings.effects_volume = volume(j, "effects_volume", settings.effects_volume);
    settings.music_volume = volume(j, "music_volume", settings.music_volume);
    return settings;
}

bool write_settings_file(const std::filesystem::path& path, const Settings& settings, std::string& error) {
    const nlohmann::ordered_json j = {{"effects_volume", settings.effects_volume},
                                      {"music_volume", settings.music_volume}};
    return write_file_atomically(path, [&](std::ostream& out) { out << j.dump() << '\n'; }, error);
}
