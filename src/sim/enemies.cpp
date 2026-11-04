// enemies.cpp — the enemy designs (see enemies.h).
#include "sim/enemies.h"

#include <cstddef>

const EnemyDesign& enemy_design(EnemyClass c) {
    using enum ItemKind;
    using enum TargetGroup;
    static const EnemyDesign designs[kEnemyClassCount] = {
        {.name = "Scout",
         .description = "A lone picket, lightly armed and unshielded. It goes for your weapons.",
         .layout = "scout",
         .hull = 700,
         .weapons = {BurstLaser},
         .extras = {CargoHold},
         .priorities = {Weapons, Bridge},
         .min_threat = 1,
         .max_threat = 2,
         .pace = 80,
         .schematics = {HeavyLaser},
         .schematic_chance = 0.35f},
        {.name = "Raider",
         .description = "A pirate with one shield. It shoots out your engines first, so you can't run.",
         .layout = "raider",
         .hull = 1000,
         .weapons = {BurstLaser, BurstLaser},
         .shields = 1,
         .extras = {CargoHold, CargoHold, SolarFurnace},
         .priorities = {Engines, Weapons, Bridge},
         .min_threat = 2,
         .max_threat = 3,
         .pace = 80,
         .schematics = {MissileLauncher, HeavyLaser},
         .schematic_chance = 0.6f},
        {.name = "Gunship",
         .description = "Heavy lasers and a missile launcher: it disarms you, then breaks your shields.",
         .layout = "gunship",
         .hull = 1400,
         .weapons = {HeavyLaser, MissileLauncher},
         .shields = 1,
         .extras = {CargoHold, FabricatorMk1},
         .priorities = {Weapons, Shields, Bridge},
         .min_threat = 2,
         .max_threat = 4,
         .pace = 90,
         .schematics = {MissileLauncher, IonBlaster, HeavyLaser},
         .schematic_chance = 0.8f},
        {.name = "Corsair",
         .description = "Two shields, ion and flak: it strips your shields bare, then your guns.",
         .layout = "corsair",
         .hull = 1700,
         .weapons = {IonBlaster, BurstLaser, FlakCannon},
         .shields = 2,
         .extras = {CargoHold, CargoHold, Refinery},
         .priorities = {Shields, Weapons, Engines, Bridge},
         .min_threat = 3,
         .max_threat = 5,
         .pace = 95,
         .schematics = {IonBlaster, FlakCannon, MissileLauncher},
         .schematic_chance = 0.9f},
        {.name = "Dreadnought",
         .description = "A capital warship behind three shields, with a beam. It aims for your bridge.",
         .layout = "dreadnought",
         .hull = 2000,
         .weapons = {BeamEmitter, HeavyLaser, MissileLauncher},
         .shields = 3,
         .extras = {CargoHold, CargoHold, FabricatorMk2, Refinery},
         .priorities = {Bridge, Weapons},
         .min_threat = 4,
         .max_threat = 5,
         .pace = 90,
         .schematics = {BeamEmitter, FlakCannon, IonBlaster},
         .schematic_chance = 1.0f},
    };
    return designs[static_cast<std::size_t>(c)];
}

// No default case, so a new EnemyClass without an id is a -Wswitch error.
const char* enemy_id(EnemyClass c) {
    switch (c) {
    case EnemyClass::Scout:       return "Scout";
    case EnemyClass::Raider:      return "Raider";
    case EnemyClass::Gunship:     return "Gunship";
    case EnemyClass::Corsair:     return "Corsair";
    case EnemyClass::Dreadnought: return "Dreadnought";
    case EnemyClass::Count:       break;
    }
    return "?";
}

std::optional<EnemyClass> enemy_from_id(std::string_view id) {
    for (int i = 0; i < kEnemyClassCount; ++i)
        if (id == enemy_id(static_cast<EnemyClass>(i)))
            return static_cast<EnemyClass>(i);
    return std::nullopt;
}

std::vector<EnemyClass> enemies_for_threat(int threat) {
    std::vector<EnemyClass> found;
    for (int i = 0; i < kEnemyClassCount; ++i) {
        const EnemyDesign& d = enemy_design(static_cast<EnemyClass>(i));
        if (threat >= d.min_threat && threat <= d.max_threat)
            found.push_back(static_cast<EnemyClass>(i));
    }
    return found;
}
