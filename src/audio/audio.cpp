// audio.cpp — decoding the embedded sounds and mixing them on SDL's audio
// thread (see audio.h).
//
// Every clip is decoded once, at Init(), to mono float samples at the
// device's rate (SDL converts whatever the WAV holds), so mixing is a sum.
// The audio thread reads the voices; the main thread starts and stops them
// with the device locked.
#include "audio/audio.h"

#include "audio/embedded_sounds.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace audio {

namespace {

constexpr int kRate = 44100;          // what the mixer runs at (and the effects are made at)
constexpr Uint16 kBufferFrames = 1024;  // per callback: about 23 ms of latency

// A decoded sound: mono float samples at kRate.
struct Clip {
    std::vector<float> samples;
};

// A music track playing, looping: where it's got to, and its fade.
struct Track {
    const Clip* clip = nullptr;  // null: free
    std::size_t pos = 0;
    float level = 0.0f;   // 0 to 1, moving toward `target` by kFadeStep a sample
    float target = 0.0f;  // 1: fading in (or playing), 0: fading out, then freed
};
// How far a track's level moves each sample: across 0..1 in kCrossfadeMs.
constexpr float kFadeStep = 1000.0f / (static_cast<float>(kRate) * static_cast<float>(kCrossfadeMs));

// A clip playing: where it's got to, and how loud.
struct Voice {
    const Clip* clip = nullptr;  // null: free
    std::size_t pos = 0;
    float gain = 1.0f;
    std::uint64_t started = 0;  // a serial, to find the oldest
};

struct State {
    SDL_AudioDeviceID device = 0;  // 0: no audio, everything is a no-op
    std::array<Clip, kSoundCount> sounds;
    std::array<Clip, kMusicCount> music;

    // Read by the audio thread: changed only with the device locked.
    std::array<Voice, kMaxVoices> voices;
    std::array<Track, 2> tracks;  // the music: one playing, maybe one fading out
    float effects_gain = 0.0f;
    float music_gain = 0.0f;

    // The main thread's own.
    Volumes volumes;
    std::optional<Music> playing;
    std::array<Uint32, kSoundCount> last_start{};  // SDL_GetTicks() of each sound's last start
    std::uint64_t serial = 0;
};

State g;

// RAII for SDL_LockAudioDevice: the audio thread doesn't run while held.
class DeviceLock {
public:
    DeviceLock() { SDL_LockAudioDevice(g.device); }
    ~DeviceLock() { SDL_UnlockAudioDevice(g.device); }
    DeviceLock(const DeviceLock&) = delete;
    DeviceLock& operator=(const DeviceLock&) = delete;
};

// The embedded WAV at `path`, decoded to a Clip; empty (and reported) if it
// is missing or can't be read.
Clip Decode(const std::string& path) {
    Clip clip;
    const std::span<const unsigned char> file = EmbeddedSound(path);
    SDL_AudioSpec spec;
    Uint8* data = nullptr;
    Uint32 size = 0;
    if (file.empty() ||
        !SDL_LoadWAV_RW(SDL_RWFromConstMem(file.data(), static_cast<int>(file.size())), 1, &spec, &data, &size)) {
        std::printf("Sound %s: %s\n", path.c_str(), file.empty() ? "not embedded" : SDL_GetError());
        return clip;
    }
    SDL_AudioStream* stream = SDL_NewAudioStream(spec.format, spec.channels, spec.freq, AUDIO_F32SYS, 1, kRate);
    if (stream && SDL_AudioStreamPut(stream, data, static_cast<int>(size)) == 0 && SDL_AudioStreamFlush(stream) == 0) {
        clip.samples.resize(static_cast<std::size_t>(SDL_AudioStreamAvailable(stream)) / sizeof(float));
        const int got = SDL_AudioStreamGet(stream, clip.samples.data(),
                                           static_cast<int>(clip.samples.size() * sizeof(float)));
        clip.samples.resize(static_cast<std::size_t>(std::max(got, 0)) / sizeof(float));
    } else {
        std::printf("Sound %s: %s\n", path.c_str(), SDL_GetError());
    }
    SDL_FreeAudioStream(stream);
    SDL_FreeWAV(data);
    return clip;
}

// A volume slider's 0..1 as an amplitude: squared, so the slider's middle
// sounds about half as loud rather than barely quieter.
float Amplitude(float volume) {
    const float v = std::clamp(volume, 0.0f, 1.0f);
    return v * v;
}

// The audio thread: fills `bytes` with the music and every effect playing.
void Mix(void*, Uint8* bytes, int len) {
    float* out = reinterpret_cast<float*>(bytes);
    const std::size_t n = static_cast<std::size_t>(len) / sizeof(float);
    std::fill(out, out + n, 0.0f);

    for (Track& t : g.tracks) {
        if (!t.clip)
            continue;
        const std::vector<float>& s = t.clip->samples;
        for (std::size_t i = 0; i < n && !s.empty(); ++i) {
            if (t.level < t.target)
                t.level = std::min(t.target, t.level + kFadeStep);
            else if (t.level > t.target)
                t.level = std::max(t.target, t.level - kFadeStep);
            out[i] += s[t.pos] * t.level * g.music_gain;
            if (++t.pos == s.size())
                t.pos = 0;  // the tracks are made to loop seamlessly
        }
        if (t.target == 0.0f && t.level == 0.0f)
            t = Track{};
    }
    for (Voice& v : g.voices) {
        if (!v.clip)
            continue;
        const std::vector<float>& s = v.clip->samples;
        const std::size_t count = std::min(n, s.size() - v.pos);
        const float gain = v.gain * g.effects_gain;
        for (std::size_t i = 0; i < count; ++i)
            out[i] += s[v.pos + i] * gain;
        v.pos += count;
        if (v.pos >= s.size())
            v.clip = nullptr;
    }
    for (std::size_t i = 0; i < n; ++i)
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

}  // namespace

bool Init() {
    if (g.device)
        return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::printf("No sound: SDL_InitSubSystem(audio): %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want{};
    want.freq = kRate;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = kBufferFrames;
    want.callback = Mix;
    // No allowed changes: SDL converts to whatever the hardware wants.
    g.device = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    if (!g.device) {
        std::printf("No sound: SDL_OpenAudioDevice(): %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    for (int i = 0; i < kSoundCount; ++i)
        g.sounds[static_cast<std::size_t>(i)] = Decode(std::string("sfx/") + kSoundNames[static_cast<std::size_t>(i)] + ".wav");
    for (int i = 0; i < kMusicCount; ++i)
        g.music[static_cast<std::size_t>(i)] = Decode(std::string("music/") + kMusicNames[static_cast<std::size_t>(i)] + ".wav");
    SetVolumes(g.volumes);
    SDL_PauseAudioDevice(g.device, 0);
    return true;
}

void Shutdown() {
    if (!g.device)
        return;
    SDL_CloseAudioDevice(g.device);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    g = State{};
}

void Play(Sound sound, float gain) {
    const std::size_t index = static_cast<std::size_t>(sound);
    if (!g.device || g.sounds[index].samples.empty())
        return;
    const Uint32 now = SDL_GetTicks();
    if (g.last_start[index] != 0 && now - g.last_start[index] < kRetriggerMs)
        return;
    g.last_start[index] = std::max<Uint32>(now, 1);
    DeviceLock lock;
    // A free voice, or else the oldest.
    Voice* voice = &g.voices.front();
    for (Voice& v : g.voices) {
        if (!v.clip) {
            voice = &v;
            break;
        }
        if (v.started < voice->started)
            voice = &v;
    }
    *voice = Voice{.clip = &g.sounds[index], .pos = 0, .gain = gain, .started = ++g.serial};
}

void PlayMusic(Music music) {
    if (!g.device || g.playing == music)
        return;
    g.playing = music;
    const Clip* clip = &g.music[static_cast<std::size_t>(music)];
    DeviceLock lock;
    // Everything else fades out; this one fades back in if it's still
    // fading out, or else starts in a free slot (or the quieter one).
    Track* slot = nullptr;
    for (Track& t : g.tracks) {
        t.target = 0.0f;
        if (t.clip == clip)
            slot = &t;
    }
    if (!slot) {
        slot = &g.tracks.front();
        for (Track& t : g.tracks)
            if (!t.clip || t.level < slot->level)
                slot = &t;
        *slot = Track{.clip = clip, .pos = 0, .level = 0.0f, .target = 1.0f};
    }
    slot->target = 1.0f;
}

void StopMusic() {
    if (!g.device || !g.playing)
        return;
    g.playing.reset();
    DeviceLock lock;
    for (Track& t : g.tracks)
        t.target = 0.0f;
}

Volumes GetVolumes() {
    return g.volumes;
}

void SetVolumes(const Volumes& volumes) {
    g.volumes = volumes;
    if (!g.device)
        return;
    DeviceLock lock;
    g.effects_gain = Amplitude(volumes.effects);
    g.music_gain = Amplitude(volumes.music);
}

}  // namespace audio
