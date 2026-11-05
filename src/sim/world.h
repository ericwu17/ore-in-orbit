// world.h — World: owns all simulation state and drives it tick by tick.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// Travel: a ship jumps from its sector to a linked one, at once, when every
// ion thruster its layout has is online and charged with fuel cells; the
// jump burns all their fuel.
// Survey scanners survey the sectors next to the ship's, once a second.
//
// Combat (see combat.h): the player's ships in a sector with an enemy ship
// fight it; a ship whose bridge is destroyed is lost, and with the last one,
// the game (fleet_lost). Beating it, its salvage goes to one of them, its
// weapons found by the fleet, and the sector is cleared. Where no
// enemy is, buildings mend, kRepairPercent of their health a second.
#pragma once

#include "sim/combat.h"
#include "sim/enemies.h"
#include "sim/fleet.h"
#include "sim/game_types.h"
#include "sim/rng.h"
#include "sim/sector.h"
#include "sim/ship.h"

#include <cstdint>
#include <optional>
#include <vector>

// How much of its health a damaged building mends a second, while no enemy
// is on its ship's grid.
constexpr int kRepairPercent = 1;

// Why a jump did (or didn't) happen.
enum class JumpResult {
    Ok,
    NoSuchShip,
    NoSuchSector,
    NotLinked,        // not a neighbor of the ship's sector
    NoThrusters,
    EngineOffline,    // some thruster is shot down, not mended yet
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
    Rng rng;  // the fights' (enemy ships, where shots land)
    // The fights going on: one per sector with an enemy and any of the
    // fleet's ships.
    std::vector<Engagement> engagements;
    // What beaten enemies left, for the UI to announce (not saved).
    std::vector<Salvage> salvaged;

    // Every ship's collectors gather at its sector's yields.
    void update_yields();
    // Ships with a survey scanner survey the sectors next to theirs.
    void scan();
    // The fleet's ships in sector `sector`.
    std::vector<FleetShip*> ships_in(int sector);
    // Starts a fight wherever a ship of the fleet meets an enemy, ends
    // those it has left, and steps the rest; ships whose bridge falls are
    // lost.
    void fight();
    // A beaten enemy's salvage: into its ship's bridge, the weapons in it
    // found by the fleet, the sector cleared.
    void collect(Salvage salvage);

public:
    // A new game on the map kDefaultMapSeed makes.
    World() : World(kDefaultMapSeed) {}
    // A new game on the map `map_seed` makes.
    explicit World(std::uint64_t map_seed);

    // One fixed simulation step (kTicksPerSecond of them per real second).
    // Nothing happens once the fleet is lost.
    void advance_tick();
    // Every ship has been lost: the game is over.
    bool fleet_lost() const { return fleet.get_ships().empty(); }

    Tick get_tick() const { return current_tick; }

    Fleet& get_fleet() { return fleet; }
    const Fleet& get_fleet() const { return fleet; }
    // The fleet's oldest ship (a new game's only one).
    Ship& flagship() { return fleet.flagship().ship; }
    const Ship& flagship() const { return fleet.flagship().ship; }

    const StarMap& get_map() const { return map; }
    // Surveys every sector at once (the debug menu).
    void reveal_map();
    // The fight in sector `sector`, or null if there's none.
    const Engagement* engagement_in(int sector) const;
    // Every fight going on.
    const std::vector<Engagement>& get_engagements() const { return engagements; }
    // Sets what ship `ship`'s weapons fire at: the building covering `cell`
    // on the enemy in its sector (nullopt: none, they pick). False if
    // there's no such ship, no fight there or no building on that cell.
    bool set_target(int ship, std::optional<GridPos> cell);
    // What beaten enemies left since the last call, oldest first (for the
    // UI to announce).
    std::vector<Salvage> take_salvage();
    // Puts an enemy of class `enemy` in ship `ship`'s sector, in place of
    // any there (the debug menu); false if there's no such ship.
    bool spawn_enemy(int ship, EnemyClass enemy);
    // Beats the enemy in ship `ship`'s sector at once, as if its bridge
    // were shot down (the debug menu); false if there's no fight there.
    bool defeat_enemy(int ship);

    // Whether jump(ship, sector) would succeed.
    JumpResult can_jump(int ship, int sector) const;
    // Moves ship `ship` (an id) to `sector`, burning all its thrusters'
    // fuel, and its weapons lose their charge; the sector is visited from
    // now on. A fight it leaves goes on without it (or ends, with no ship
    // of the fleet left there); one in the new sector starts.
    JumpResult jump(int ship, int sector);
};
