// combat.h — ship-to-ship fights: an enemy ship (see enemies.h) against the
// player's ships in its sector, weapon against weapon.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// A sector may hold an enemy ship. While any of the player's ships is
// there, the two sides fight (an Engagement); once the last one leaves,
// the enemy is left as it was found, to fight afresh another time.
//
// Every weapon charges while it has fuel and ammunition (an enemy's need
// neither), then fires a volley and starts again. The player's weapons fire
// at the building their ship targets on the enemy (FleetShip::target), or
// with none, at its shields, then its weapons, then its bridge. An enemy's
// fire at a player ship there, each weapon at the building its design's
// priorities pick (see enemies.h). Shots take a moment to fly (their
// WeaponSpec's flight), then land on the cell they were aimed at:
//
//   - A shield layer up stops a laser shot or a flak pellet and is knocked
//     down (see ShieldProjector); an ion bolt knocks one down and stuns its
//     projector. Missiles go through shields; beams are weakened by every
//     layer up, without knocking any down.
//   - Past the shields, a shot may miss: every online ion thruster dodges
//     kEvasionPerEngine of them, up to kMaxEvasion. Beams never miss.
//   - Then it hits the building on that cell (nothing, on bare hull).
//     Shot down to 0, a building is destroyed for good, refunding nothing,
//     except an engine, knocked offline till it mends, and the bridge: at
//     0, its ship is lost (the player's, see World) or beaten (the enemy).
//
// A beaten enemy leaves salvage for the player ship that fired the last
// shot: materials and fuel by the sector's threat, ammunition for its
// weapons, some of its weapons that are still standing, and maybe a weapon
// from its hold, one the fleet hasn't found if it can. Salvage is the only
// way to get weapons (see milestones.h).
#pragma once

#include "sim/enemies.h"
#include "sim/fleet.h"
#include "sim/game_types.h"
#include "sim/milestones.h"
#include "sim/rng.h"
#include "sim/ship.h"

#include <optional>
#include <vector>

// Every online engine dodges this much of what's fired at its ship...
constexpr float kEvasionPerEngine = 0.05f;
// ...up to this much.
constexpr float kMaxEvasion = 0.3f;
// What each shield layer up takes off each hit of a beam.
constexpr int kBeamShieldReduction = 40;
// How often an enemy weapon shoots at any building at all, instead of its
// design's priorities.
constexpr float kRandomTargetChance = 0.2f;
// The chance of each weapon still standing on a beaten enemy coming with
// its salvage.
constexpr float kWeaponSalvageChance = 0.5f;
// How long a landed shot's flash shows (the UI; not saved).
constexpr Tick kImpactTicks = kTicksPerSecond / 2;

// A shot on its way: a laser shot, a missile, a flak pellet, an ion bolt or
// a beam.
struct Shot {
    bool at_enemy = false;  // fired by the player's ship at the enemy, or the other way
    int ship = 0;           // the player's ship: firing it (at_enemy), or aimed at
    ItemKind weapon = ItemKind::BurstLaser;  // what fired it
    GridPos cell;           // where it lands, on the target ship's grid
    Direction sweep = Direction::East;  // a beam: the way it sweeps from `cell`
    Tick fired = 0;
    Tick lands = 0;
};

// How a shot landed, for the UI.
enum class ImpactKind {
    Hit,
    Blocked,  // by a shield layer
    Missed,   // dodged
};

// A shot that just landed, for the UI to show (not saved).
struct Impact {
    bool on_enemy = false;
    int ship = 0;  // the player's ship: the target's (on_enemy false)
    GridPos cell;
    ImpactKind kind = ImpactKind::Hit;
    ItemKind weapon = ItemKind::BurstLaser;
    Direction sweep = Direction::East;
    Tick at = 0;
};

// The fight in one sector: its enemy ship and the shots flying.
struct Engagement {
    int sector = 0;
    EnemyClass enemy = EnemyClass::Scout;
    Ship ship;  // the enemy's
    std::vector<Shot> shots;
    std::vector<Impact> impacts;  // landed in the last kImpactTicks (not saved)

    const EnemyDesign& design() const { return enemy_design(enemy); }
};

// What a beaten enemy left.
struct Salvage {
    EnemyClass enemy = EnemyClass::Scout;
    int sector = 0;
    int ship = 0;  // the player's ship it went to
    std::vector<ItemStack> items;
    // The kinds of weapon among `items` new to the fleet (filled in when
    // it's collected, see World).
    std::vector<ItemKind> found;
};

// A new enemy of class `enemy` in sector `sector`: its layout, with its
// weapons, shields and other buildings placed where `rng` puts them, and its
// weapons part charged.
Engagement make_engagement(int sector, EnemyClass enemy, Rng& rng);

// The shield layers up round `ship`: one per shield projector up.
int shield_layers(const Ship& ship);
// The share of shots fired at `ship` that miss (see the top of this file).
float evasion(const Ship& ship);
// Whether the building anchored at `p` on `ship` belongs to `group`.
bool in_target_group(const Ship& ship, GridPos p, TargetGroup group);
// The top-left cell of the building on `target` to shoot at: one of the
// first group in `priorities` with any standing, picked by `rng` (with
// chance `random`, one of any standing); nullopt if there's none.
std::optional<GridPos> pick_target(const Ship& target, const std::vector<TargetGroup>& priorities, float random,
                                   Rng& rng);
// What the player's weapons fire at without a target of their ship's.
const std::vector<TargetGroup>& default_player_priorities();

// One step of `fight` against `ships` (the player's ships in its sector,
// each already ticked this step) at World tick `now`: weapons charge and
// fire, shots land. Returns the salvage once the enemy is beaten (its
// bridge at 0), going to one of `ships`; `progress` says which weapons are
// found already, and `threat` (the sector's) how rich it is.
std::optional<Salvage> combat_tick(Engagement& fight, const std::vector<FleetShip*>& ships, int threat,
                                   const Progress& progress, Rng& rng, Tick now);
