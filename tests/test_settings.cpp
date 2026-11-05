// test_settings.cpp — headless tests for the settings file
// (persistence/settings_file.h): a round trip, and the defaults for a file
// that's missing, broken or out of range.
#include "persistence/settings_file.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path scratch_file() {
    const fs::path dir = fs::temp_directory_path() / "ore-in-orbit-test-settings";
    fs::create_directories(dir);
    return dir / "settings.json";
}

void write_text(const fs::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

void test_round_trip() {
    const fs::path path = scratch_file();
    std::string error;
    assert(write_settings_file(path, {.effects_volume = 0.25f, .music_volume = 0.75f}, error));
    const Settings read = read_settings_file(path);
    assert(read.effects_volume == 0.25f && read.music_volume == 0.75f);
}

void test_defaults() {
    const Settings defaults;
    const fs::path path = scratch_file();
    fs::remove(path);
    Settings read = read_settings_file(path);
    assert(read.effects_volume == defaults.effects_volume && read.music_volume == defaults.music_volume);
    write_text(path, "not json at all");
    read = read_settings_file(path);
    assert(read.effects_volume == defaults.effects_volume && read.music_volume == defaults.music_volume);
    // One value missing, one out of range: the default, and clamped.
    write_text(path, R"({"music_volume": 7})");
    read = read_settings_file(path);
    assert(read.effects_volume == defaults.effects_volume && read.music_volume == 1.0f);
    write_text(path, R"({"effects_volume": "loud", "music_volume": -1})");
    read = read_settings_file(path);
    assert(read.effects_volume == defaults.effects_volume && read.music_volume == 0.0f);
    fs::remove_all(path.parent_path());
}

}  // namespace

int main() {
    test_round_trip();
    test_defaults();
    std::puts("test_settings: all passed");
    return 0;
}
