// save_file.h — one saved game as a JSON document: a short header (what the
// save list shows) and the whole World. The format, and all the code that
// knows it, is in save_file.cpp.
// Headless like the simulation: no imgui/SDL/OpenGL headers.
//
//   {
//     "format": "ore-in-orbit-save",
//     "version": 1,
//     "name": "Ship 1",
//     "saved_at": 1760000000,      (Unix seconds)
//     "played": 3600,              (ticks: a copy of the World's, for the list)
//     "world": { "tick": 3600,
//       "star_map": { "sectors": [ { "name": "Home", "x": 0, "y": 0, "neighbors": [1, 2],
//                                    "yields": {...}, "threat": 0, "visited": true,
//                                    "surveyed": true }, ... ] },
//       "fleet": {
//       "next_ship_id": 2, "progress": { "milestone": 0, "delivered": {...} },
//       "ships": [ { "id": 1, "name": "Flagship", "sector": 0,
//                    "ship": { "hull": [...], "buildings": [...] } } ],
//       "transfers": [ { "from": 1, "to": 2, "items": {...}, "arrives": 3900 } ] } }
//   }
#pragma once

#include "sim/game_types.h"
#include "sim/world.h"

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>

// The version write_save() writes; read_save() reads only this one. Bump it
// when the format changes (or an item or recipe is renamed or removed, or a
// building's footprint changes, which can leave saved ones overlapping or
// off their hull).
constexpr int kSaveVersion = 7;

// What the save list shows about a save, read without loading the World.
struct SaveHeader {
    std::string name;
    std::int64_t saved_at = 0;  // Unix seconds
    Tick played = 0;            // the World's tick when saved
};

void write_save(std::ostream& out, const std::string& name, std::int64_t saved_at, const World& world);

// The header alone. On failure, `error` says why.
std::optional<SaveHeader> read_save_header(std::istream& in, std::string& error);

struct LoadedSave {
    SaveHeader header;
    World world;
};
// The header and the World. On failure, `error` says why.
std::optional<LoadedSave> read_save(std::istream& in, std::string& error);
