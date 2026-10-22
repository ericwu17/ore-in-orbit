// blueprint_file.h — blueprints as text, and the blueprint library folder.
// Headless like the simulation: no imgui/SDL/OpenGL headers.
//
// A blueprint is one line of JSON, the string players export and import
// (and the whole of a library file):
//
//   {"format":"ore-in-orbit-blueprint","version":2,"name":"Iron line",
//    "width":4,"height":2,
//    "buildings":[{"kind":"TransportBelt","x":0,"y":0,"facing":"East"},
//                 {"kind":"SolarFurnace","x":2,"y":0,"facing":"North","recipe":"Iron plate"}]}
//
// Buildings carry "recipe" (machines), "target" (collectors) and "filter"
// (sorters) when set. Items, directions and recipes are written by name,
// as saves write them (persistence/json_io.h). Reading checks everything a
// paste relies on, and refuses a bad string with a reason, never an assert.
//
// The library is a folder (main.cpp uses blueprints/ in SDL's per-user
// app-data directory, next to saves/) with one "blueprint-<N>.json" per
// blueprint: the file name is just an id, the name is inside it.
#pragma once

#include "sim/blueprint.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The version blueprint_to_string() writes. It reads this one, and version
// 1, which also had hull rows (left out: the hull is the ship's layout's).
constexpr int kBlueprintVersion = 2;

std::string blueprint_to_string(const Blueprint& blueprint);
// On failure, `error` says why.
std::optional<Blueprint> blueprint_from_string(std::string_view text, std::string& error);

// One file in the library, and its blueprint, or why it can't be read.
struct BlueprintSlot {
    std::filesystem::path path;
    std::optional<Blueprint> blueprint;
    std::string error;
};

// Every blueprint in `dir`, by name, then the unreadable ones (by file
// name). Empty if the folder doesn't exist yet.
std::vector<BlueprintSlot> list_blueprints(const std::filesystem::path& dir);

// Writes `blueprint` to a new file in `dir` (creating it if needed), and
// returns that file.
std::optional<std::filesystem::path> add_blueprint(const std::filesystem::path& dir, const Blueprint& blueprint,
                                                   std::string& error);

bool rename_blueprint(const std::filesystem::path& path, const std::string& new_name, std::string& error);

bool delete_blueprint(const std::filesystem::path& path, std::string& error);
