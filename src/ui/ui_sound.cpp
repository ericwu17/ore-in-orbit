// ui_sound.cpp — what the game sounds like: which sound plays for what the
// player does and for what happens in the simulation, and the music for each
// screen. audio/audio.h does the playing.
//
// Three sources:
//   - The player's actions: the code handling one calls PlayUi() with its
//     sound (placing a building, a failed click, switching ships...).
//   - Anything else clicked: PlayFrameSounds() clicks for a press on any
//     widget that played nothing of its own, so every button sounds without
//     each one asking.
//   - The simulation's events (sim/events.h), taken once a frame: a fight is
//     heard only in the sector of the ship on screen, and a ship's own
//     business (cargo, crafting, engines) only on that ship; the fleet's big
//     moments (milestones, enemies met and beaten, ships lost) anywhere.
#include "ui/ui_internal.h"
#include "ui/ui.h"

#include "audio/audio.h"
#include "audio/sounds.h"

#include <optional>

namespace ui {

namespace {

using audio::Sound;

// A UI sound was played this frame (PlayUi), so the generic click stays out.
bool g_played_ui_sound = false;

// Where an event is heard (see the top of this file).
enum class Heard {
    Anywhere,
    InShownSector,  // only in the sector of the ship on screen
    OnShownShip,    // only for the ship on screen
};

// The sound an event makes: which, how loud, and where it's heard.
struct Cue {
    Sound sound;
    float gain = 1.0f;
    Heard heard = Heard::Anywhere;
};

// The sound of weapon `weapon` firing.
Sound FiringSound(ItemKind weapon) {
    switch (weapon) {
    case ItemKind::HeavyLaser:      return Sound::CombatHeavyLaser;
    case ItemKind::MissileLauncher: return Sound::CombatMissileLaunch;
    case ItemKind::IonBlaster:      return Sound::CombatIonBlaster;
    case ItemKind::FlakCannon:      return Sound::CombatFlakCannon;
    case ItemKind::BeamEmitter:     return Sound::CombatBeamEmitter;
    default:                        return Sound::CombatBurstLaser;
    }
}

// What `e` sounds like, and where it's heard; nullopt for silence. The
// enemy's side of a fight is a little quieter than the player's: what
// happens to your own ship matters more.
std::optional<Cue> CueFor(const Event& e) {
    const float side = e.enemy ? 0.7f : 1.0f;
    switch (e.kind) {
    case EventKind::WeaponFired:       return Cue{FiringSound(e.item), side, Heard::InShownSector};
    case EventKind::ShotHit:           return Cue{Sound::CombatHitHull, side, Heard::InShownSector};
    case EventKind::ShotBlocked:       return Cue{Sound::CombatHitShield, side, Heard::InShownSector};
    case EventKind::ShotMissed:        return Cue{Sound::CombatMiss, side, Heard::InShownSector};
    case EventKind::ShieldsDown:       return Cue{Sound::CombatShieldDown, side, Heard::InShownSector};
    case EventKind::ShieldUp:          return Cue{Sound::CombatShieldRecharge, side * 0.7f, Heard::InShownSector};
    case EventKind::BuildingDestroyed: return Cue{Sound::CombatBuildingDestroyed, side, Heard::InShownSector};
    case EventKind::EngineOffline:     return Cue{Sound::CombatEngineOffline, side, Heard::InShownSector};
    case EventKind::EnemyEngaged:      return Cue{Sound::GameEnemyDetected, 1.0f, Heard::Anywhere};
    case EventKind::EnemyBeaten:       return Cue{Sound::CombatEnemyDestroyed, 1.0f, Heard::Anywhere};
    case EventKind::ShipLost:          return Cue{Sound::CombatShipLost, 1.0f, Heard::Anywhere};
    case EventKind::FleetLost:         return Cue{Sound::GameOver, 1.0f, Heard::Anywhere};
    case EventKind::MilestoneComplete: return Cue{Sound::GameMilestoneComplete, 1.0f, Heard::Anywhere};
    case EventKind::CraftComplete:     return Cue{Sound::GameCraftComplete, 1.0f, Heard::OnShownShip};
    case EventKind::CargoLanded:       return Cue{Sound::GameCargoLand, 1.0f, Heard::OnShownShip};
    case EventKind::ThrustersCharged:  return Cue{Sound::GameThrustersCharged, 1.0f, Heard::OnShownShip};
    case EventKind::Jumped:            return Cue{Sound::GameJump, 1.0f, Heard::OnShownShip};
    case EventKind::SectorSurveyed:    return Cue{Sound::GameSectorSurveyed, 0.7f, Heard::Anywhere};
    }
    return std::nullopt;
}

}  // namespace

void PlayUi(audio::Sound sound, float gain) {
    audio::Play(sound, gain);
    g_played_ui_sound = true;
}

bool SoundCheckbox(const char* label, bool* v) {
    if (!ImGui::Checkbox(label, v))
        return false;
    PlayUi(*v ? Sound::UiToggleOn : Sound::UiToggleOff);
    return true;
}

void VolumeSliders(float width) {
    audio::Volumes v = audio::GetVolumes();
    // As percentages, which read better than fractions.
    const auto slider = [&](const char* label, float& volume) {
        int percent = static_cast<int>(volume * 100.0f + 0.5f);
        ImGui::SetNextItemWidth(width);
        const bool changed = ImGui::SliderInt(label, &percent, 0, 100, "%d%%");
        if (changed)
            volume = static_cast<float>(percent) / 100.0f;
        return changed;
    };
    bool changed = slider("Effects", v.effects);
    // Let go of the effects slider: a sound at the new volume, to judge it by.
    const bool try_effects = ImGui::IsItemDeactivatedAfterEdit();
    changed |= slider("Music", v.music);
    if (changed)
        audio::SetVolumes(v);
    if (try_effects)
        PlayUi(Sound::UiConfirm);
}

void PlayEventSounds(World& world) {
    // The ship on screen, if it's still there (it may have just been lost).
    const FleetShip* shown = world.get_fleet().find(g_fleet.current);
    for (const Event& e : world.take_events()) {
        const std::optional<Cue> cue = CueFor(e);
        if (!cue)
            continue;
        if (cue->heard == Heard::InShownSector && (!shown || shown->sector != e.sector))
            continue;
        if (cue->heard == Heard::OnShownShip && (!shown || shown->id != e.ship))
            continue;
        audio::Play(cue->sound, cue->gain);
    }
}

}  // namespace ui

using namespace ui;

void PlayFrameSounds(Screen screen, const World& world) {
    // The music, except over the wreck of a lost fleet (its game-over sound
    // plays alone).
    if (screen == Screen::Playing && world.fleet_lost())
        audio::StopMusic();
    else
        audio::PlayMusic(audio::Music::ColdOrbit);

    // A press on any widget that played nothing of its own clicks; typing
    // ticks.
    const ImGuiIO& io = ImGui::GetIO();
    if (!g_played_ui_sound && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsAnyItemActive())
        audio::Play(Sound::UiClick);
    if (io.WantTextInput && !io.InputQueueCharacters.empty())
        audio::Play(Sound::UiType);
    g_played_ui_sound = false;
}
