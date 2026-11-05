// audio.h — playing the game's sounds (see sounds.h) through SDL's audio
// device: a small software mixer, with one looping music track under any
// number of effects. src/audio/ is the only code that touches SDL audio; the
// UI decides what to play and when (ui/ui_sound.h).
//
// Every call is safe without a device (none found, or before Init()): it
// just plays nothing, so the game runs silent rather than not at all.
#pragma once

#include "audio/sounds.h"

namespace audio {

// Opens the default audio device and decodes the embedded sounds. Call once,
// after SDL_Init(). False (and a message on stdout) if there's no device:
// everything below then does nothing.
bool Init();
// Stops everything and closes the device.
void Shutdown();

// Starts effect `sound` at `gain` (1: as made; the effects volume applies on
// top). The same sound started again within kRetriggerMs is dropped, so a
// burst of events (a flak volley's pellets, a belt dragged across the grid)
// sounds once rather than as a roar; with kMaxVoices effects playing, the
// oldest gives way.
void Play(Sound sound, float gain = 1.0f);
constexpr unsigned kRetriggerMs = 45;
constexpr int kMaxVoices = 24;

// Loops `music` from the start, in place of any playing; does nothing if
// it is the one playing already.
void PlayMusic(Music music);
void StopMusic();

// The volumes, 0 to 1 (1: as made). The effects volume scales every
// effect, the music volume the track. (The game's defaults are the
// settings file's: persistence/settings_file.h.)
struct Volumes {
    float effects = 1.0f;
    float music = 1.0f;
};
Volumes GetVolumes();
void SetVolumes(const Volumes& volumes);

}  // namespace audio
