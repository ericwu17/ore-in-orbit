// world.h — World: owns all simulation state and drives it tick by tick.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// Travel: a ship jumps from its sector to a linked one, at once, when every
// ion thruster on board is charged with fuel cells, and it has at least
// thrusters_needed() of them for its size; the jump burns all their fuel.
// Survey scanners survey the sectors next to the ship's, once a second.
#pragma once

#include "sim/fleet.h"
#include "sim/game_types.h"
#include "sim/sector.h"
#include "sim/ship.h"

#include <cstdint>

// How many hull tiles one ion thruster can move.
constexpr int kHullPerThruster = 1500;

// The ion thrusters a ship with `hull_tiles` of hull needs to jump (at
// least one).
int thrusters_needed(int hull_tiles);

// Why a jump did (or didn't) happen.
enum class JumpResult {
    Ok,
    NoSuchShip,
    NoSuchSector,
    NotLinked,        // not a neighbor of the ship's sector
    NoThrusters,
    TooFewThrusters,  // fewer than thrusters_needed()
    NotCharged,       // some thruster isn't full of fuel
};

// A short player-facing explanation ("Not every thruster is charged", ...).
const char* describe(JumpResult result);

// The star map a World() is made with, for tests and anything else that
// needs the same map every time.
constexpr std::uint64_t kDefaultMapSeed = 1;

class World {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    Tick current_tick = 0;
    StarMap map;
    Fleet fleet = Fleet::new_game();

    // Every ship's collectors gather at its sector's yields.
    void update_yields();
    // Ships with a survey scanner survey the sectors next to theirs.
    void scan();

public:
    // A new game on the map kDefaultMapSeed makes.
    World() : World(kDefaultMapSeed) {}
    // A new game on the map `map_seed` makes.
    explicit World(std::uint64_t map_seed);

    // One fixed simulation step (kTicksPerSecond of them per real second).
    void advance_tick();

    Tick get_tick() const { return current_tick; }

    Fleet& get_fleet() { return fleet; }
    const Fleet& get_fleet() const { return fleet; }
    // The fleet's oldest ship (a new game's only one).
    Ship& flagship() { return fleet.flagship().ship; }
    const Ship& flagship() const { return fleet.flagship().ship; }

    const StarMap& get_map() const { return map; }
    // Surveys every sector at once (the debug menu).
    void reveal_map();

    // Whether jump(ship, sector) would succeed.
    JumpResult can_jump(int ship, int sector) const;
    // Moves ship `ship` (an id) to `sector`, burning all its thrusters'
    // fuel; the sector is visited from now on.
    JumpResult jump(int ship, int sector);
};
