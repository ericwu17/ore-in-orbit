// settings_file.h — the player's settings, kept between runs in one small
// JSON file (main.cpp uses settings.json in SDL's per-user app-data
// directory, next to saves/):
//
//   {"effects_volume":0.8,"music_volume":0.6,"keys":{"undo":"Ctrl+U"}}
//
// "keys" holds only the action keys the player rebound in the Settings menu,
// by action, as the UI names them (ui_keys.cpp); the rest keep their
// defaults.
//
// Headless like the simulation. A missing or unreadable file, or a missing
// or bad value, just means the default: settings are never worth an error.
#pragma once

#include <filesystem>
#include <map>
#include <string>

struct Settings {
    float effects_volume = 0.8f;  // 0 to 1
    float music_volume = 0.6f;
    // Rebound action keys: action name -> key ("undo" -> "Ctrl+U").
    std::map<std::string, std::string> key_bindings;
};

Settings read_settings_file(const std::filesystem::path& path);
// On failure, `error` says why.
bool write_settings_file(const std::filesystem::path& path, const Settings& settings, std::string& error);
