// sounds.h — the names of the game's sound effects and music, and the files
// they come from. Header-only and free of SDL, so the headless tests can
// check every one has its file (tests/test_sounds.cpp).
//
// The files are under assets/sounds/ (see the readme's "Sounds" section),
// made by tools/make_sounds.py, and embedded into the executable at build
// time like the sprites:
//
//   sfx/<name>.wav     one effect per Sound, named in ORE_SOUNDS below
//   music/<name>.wav   one track per Music, named in ORE_MUSIC
//
// To add a sound: add it to tools/make_sounds.py, run it, and add a line
// here. audio.h plays them.
#pragma once

#include <array>
#include <cstddef>

namespace audio {

// Every effect: X(enumerator, file name without .wav). Grouped as the files
// are: interface, building, game events, combat.
#define ORE_SOUNDS(X)                                   \
    X(UiClick, "ui_click")                              \
    X(UiToggleOn, "ui_toggle_on")                       \
    X(UiToggleOff, "ui_toggle_off")                     \
    X(UiTabSwitch, "ui_tab_switch")                     \
    X(UiPanelOpen, "ui_panel_open")                     \
    X(UiPanelClose, "ui_panel_close")                   \
    X(UiConfirm, "ui_confirm")                          \
    X(UiError, "ui_error")                              \
    X(UiType, "ui_type")                                \
    X(BuildPlace, "build_place")                        \
    X(BuildPlaceBelt, "build_place_belt")               \
    X(BuildRemove, "build_remove")                      \
    X(BuildRotate, "build_rotate")                      \
    X(BuildInvalid, "build_invalid")                    \
    X(BuildBlueprintPlace, "build_blueprint_place")     \
    X(GameCraftQueued, "game_craft_queued")             \
    X(GameMilestoneComplete, "game_milestone_complete") \
    X(GameCargoLaunch, "game_cargo_launch")             \
    X(GameCargoLand, "game_cargo_land")                 \
    X(GameShipBuilt, "game_ship_built")                 \
    X(GameShipSwitch, "game_ship_switch")               \
    X(GameThrustersCharged, "game_thrusters_charged")   \
    X(GameJump, "game_jump")                            \
    X(GameSectorSurveyed, "game_sector_surveyed")       \
    X(GameMapOpen, "game_map_open")                     \
    X(GameEnemyDetected, "game_enemy_detected")         \
    X(GameOver, "game_over")                            \
    X(CombatBurstLaser, "combat_burst_laser")           \
    X(CombatHeavyLaser, "combat_heavy_laser")           \
    X(CombatMissileLaunch, "combat_missile_launch")     \
    X(CombatIonBlaster, "combat_ion_blaster")           \
    X(CombatFlakCannon, "combat_flak_cannon")           \
    X(CombatBeamEmitter, "combat_beam_emitter")         \
    X(CombatHitHull, "combat_hit_hull")                 \
    X(CombatHitShield, "combat_hit_shield")             \
    X(CombatShieldDown, "combat_shield_down")           \
    X(CombatShieldRecharge, "combat_shield_recharge")   \
    X(CombatMiss, "combat_miss")                        \
    X(CombatBuildingDestroyed, "combat_building_destroyed") \
    X(CombatEngineOffline, "combat_engine_offline")     \
    X(CombatEnemyDestroyed, "combat_enemy_destroyed")   \
    X(CombatShipLost, "combat_ship_lost")               \
    X(CombatTargetSet, "combat_target_set")             \
    X(CombatTargetClear, "combat_target_clear")         \
    X(CombatSalvage, "combat_salvage")                  \
    X(CombatWeaponFound, "combat_weapon_found")

// Every music track, the same way: the background, and the fight.
#define ORE_MUSIC(X)         \
    X(ColdOrbit, "cold_orbit") \
    X(RedAlert, "red_alert")

#define ORE_AUDIO_ENUMERATOR(id, file) id,
#define ORE_AUDIO_FILE(id, file) file,

enum class Sound { ORE_SOUNDS(ORE_AUDIO_ENUMERATOR) };
enum class Music { ORE_MUSIC(ORE_AUDIO_ENUMERATOR) };

inline constexpr std::array kSoundNames = {ORE_SOUNDS(ORE_AUDIO_FILE)};
inline constexpr std::array kMusicNames = {ORE_MUSIC(ORE_AUDIO_FILE)};
inline constexpr int kSoundCount = static_cast<int>(kSoundNames.size());
inline constexpr int kMusicCount = static_cast<int>(kMusicNames.size());

#undef ORE_AUDIO_ENUMERATOR
#undef ORE_AUDIO_FILE

// The file name, without folder or extension ("ui_click").
inline const char* sound_name(Sound s) { return kSoundNames[static_cast<std::size_t>(s)]; }
inline const char* music_name(Music m) { return kMusicNames[static_cast<std::size_t>(m)]; }

}  // namespace audio
