// enemies.h — the enemy ships the star map holds: their classes, and how
// each is built, armed and fights (see combat.h for the fight itself).
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// Every enemy is a ship like the player's, in one of the enemy layouts (see
// enemy_layouts), with a bridge that loses it the fight at 0, weapons,
// maybe shield projectors, and a few other buildings: holds and machines,
// targets to shoot at. Its weapons need neither fuel nor ammunition, and
// charge at its design's pace. Each weapon picks its target building on a
// player ship by its design's priorities: the first group of buildings that
// has any standing, now and then anything at all.
#pragma once

#include "sim/game_types.h"

#include <optional>
#include <string_view>
#include <vector>

enum class EnemyClass {
    Scout,
    Raider,
    Gunship,
    Corsair,
    Dreadnought,
    Count,  // not a class: the number of them
};

constexpr int kEnemyClassCount = static_cast<int>(EnemyClass::Count);

// What an enemy's weapons go after, in its design's order.
enum class TargetGroup {
    Weapons,
    Shields,    // shield projectors
    Engines,    // ion thrusters still online
    Bridge,
    Industry,   // machines, collectors and cargo holds
};

struct EnemyDesign {
    const char* name;
    const char* description;
    const char* layout;               // the id of its enemy layout
    int hull = 1000;                  // its bridge's health: at 0, it's beaten
    std::vector<ItemKind> weapons;    // weapon kinds, one building each
    int shields = 0;                  // shield projectors
    std::vector<ItemKind> extras;     // other buildings on board
    std::vector<TargetGroup> priorities;
    int min_threat = 1;               // the sectors it's found in
    int max_threat = 1;
    int pace = 100;                   // how fast its weapons charge, in % of their spec
    std::vector<ItemKind> schematics; // weapons whose schematic it may carry
    float schematic_chance = 0.0f;    // that it carries one the fleet hasn't found
};

const EnemyDesign& enemy_design(EnemyClass c);
// Its stable id, as saves write it ("Scout", ...).
const char* enemy_id(EnemyClass c);
std::optional<EnemyClass> enemy_from_id(std::string_view id);
// The classes found in a sector of `threat`, lightest first (none at 0).
std::vector<EnemyClass> enemies_for_threat(int threat);
