// combat.h — enemies, the waves they come in, and the fight on a ship's
// grid between them and its turrets.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// A hostile sector (one with waves left, see Sector::waves_left) sends
// waves at every ship in it: the first kFirstWaveDelay after the ship comes
// in, then one every kWaveInterval while waves are left. A wave comes in
// from one side, on a ring just outside the ship's hull; the further the
// wave and the higher the threat, the bigger, and the heavier its ships.
//
// Enemies fly straight at the nearest building, stop within their range
// and shoot it, as often as their kind can. Turrets shoot the nearest enemy
// within theirs, while they have ammunition (see Turret). Shots hit at once:
// nothing flies. A building shot down to 0 is destroyed, refunding nothing
// (see Ship::destroy_building); an ion thruster is knocked offline instead
// (it comes with the layout, see Thruster), and enemies leave it be; the
// bridge shot down loses the ship (see World). Once the last wave is sent and its enemies are dead, the sector is
// cleared, for good.
//
// Positions are in cells, as floats: (0, 0) is the grid's top-left corner
// and (x + 0.5, y + 0.5) the middle of cell (x, y). Enemies may be off the
// grid.
#pragma once

#include "sim/game_types.h"
#include "sim/rng.h"
#include "sim/sector.h"
#include "sim/ship.h"

#include <optional>
#include <string_view>
#include <vector>

enum class EnemyKind {
    Drone,        // small and fast: closes in to shoot
    Gunship,      // shoots from further off
    Dreadnought,  // slow, tough and long-ranged
    Count,        // not a kind: the number of kinds
};

constexpr int kEnemyKindCount = static_cast<int>(EnemyKind::Count);

struct EnemySpec {
    const char* name;
    int hp = 0;
    float speed = 0.0f;  // cells per tick
    float range = 0.0f;  // cells, from the edge of its target
    int damage = 0;      // per shot
    Tick interval = 0;   // ticks between shots
    int cost = 0;        // what it takes of a wave's strength
    int min_threat = 0;  // the least threat of a sector that sends it
};

const EnemySpec& enemy_spec(EnemyKind kind);
// Its stable id, as saves write it ("Drone", ...).
const char* enemy_id(EnemyKind kind);
std::optional<EnemyKind> enemy_from_id(std::string_view id);

struct Enemy {
    EnemyKind kind = EnemyKind::Drone;
    float x = 0.0f;
    float y = 0.0f;
    int hp = 0;
    Tick cooldown = 0;               // ticks until it can shoot again
    std::optional<GridPos> target;   // the top-left cell of the building it's after

    // The last shot, for the UI (not saved): when, and where it hit.
    Tick fired_at = 0;
    float aim_x = 0.0f;
    float aim_y = 0.0f;
};

// When a ship's waves come (see the top of this file).
constexpr Tick kFirstWaveDelay = 30 * kTicksPerSecond;
constexpr Tick kWaveInterval = 45 * kTicksPerSecond;
// Enemies arrive this far outside the ship's hull, in cells.
constexpr float kSpawnMargin = 12.0f;

// One ship's fight: the enemies on its grid, and when its next wave comes.
struct Battle {
    std::vector<Enemy> enemies;
    std::optional<Tick> next_wave;  // a World tick; none while none is due

    bool under_attack() const { return !enemies.empty(); }
};

// Sends wave number `wave` (0 the first) of a sector of `threat` at `ship`.
void spawn_wave(const Ship& ship, Battle& battle, int threat, int wave, Rng& rng);

// One step of `ship`'s fight in `sector`, at World tick `now`: the next
// wave comes when due, enemies close in and shoot, turrets shoot back, and
// the dead are cleared away. Buildings shot down are destroyed; engines
// stay, offline, and the bridge stays, at 0 (see Ship::bridge_destroyed).
void combat_tick(Ship& ship, Battle& battle, Sector& sector, Rng& rng, Tick now);
