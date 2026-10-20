// save_slots.h — the saves folder: one file per saved game, and what the
// save list does with them (list, write, load, rename, copy, delete).
// Headless like the simulation: no imgui/SDL/OpenGL headers.
//
// Each save is "save-<N>.sav" in the folder (main.cpp uses saves/ in SDL's
// per-user app-data directory). The file name is just an id: the name the
// player sees is in the save's header, so renaming never moves a file.
//
// Nothing here throws: failures return false or nullopt and say why in
// `error`.
#pragma once

#include "persistence/save_file.h"
#include "sim/world.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// One file in the saves folder, and its header, or why it can't be read.
struct SaveSlot {
    std::filesystem::path path;
    std::optional<SaveHeader> header;
    std::string error;
};

// Every save in `dir`, newest first, then the unreadable ones (by file
// name). Empty if the folder doesn't exist yet.
std::vector<SaveSlot> list_saves(const std::filesystem::path& dir);

// An unused file name for a new save in `dir` (creating the folder if
// needed).
std::optional<std::filesystem::path> new_save_path(const std::filesystem::path& dir, std::string& error);

// "Ship 1", "Ship 2", ...: the first such name no save in `slots` has.
std::string next_default_name(const std::vector<SaveSlot>& slots);

// Writes the save, replacing the file only once the whole new one is on
// disk, so a crash mid-write leaves the old save intact. `saved_at`
// defaults to now.
bool write_save_file(const std::filesystem::path& path, const std::string& name, const World& world,
                     std::string& error, std::optional<std::int64_t> saved_at = std::nullopt);

std::optional<LoadedSave> load_save_file(const std::filesystem::path& path, std::string& error);

// Gives the save a new name, keeping when it was saved (so the list keeps
// its order).
bool rename_save(const std::filesystem::path& path, const std::string& new_name, std::string& error);

// Copies the save to a new file in `dir`, named "<name> (copy)". Returns the
// new file.
std::optional<std::filesystem::path> copy_save(const std::filesystem::path& dir, const std::filesystem::path& path,
                                               std::string& error);

bool delete_save(const std::filesystem::path& path, std::string& error);
