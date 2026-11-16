// test_sounds.cpp — headless checks of assets/sounds/ (see
// src/audio/sounds.h): every Sound and Music has its WAV file, and every
// file there is one of them, so the list and tools/make_sounds.py can't
// drift apart; and the music's shuffle (src/audio/playlist.h). Runs from the
// repo root, like every test.
#include "audio/playlist.h"
#include "audio/sounds.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>

namespace {

int g_failures = 0;

// Whether `path` starts like a WAV file: "RIFF", a size, "WAVE".
bool is_wav(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    char header[12] = {};
    if (!in.read(header, sizeof(header)))
        return false;
    return std::string(header, 4) == "RIFF" && std::string(header + 8, 4) == "WAVE";
}

// Every name in `names` has <folder>/<name>.wav, and nothing else is there.
template <typename Names>
void check_folder(const std::string& folder, const Names& names) {
    std::set<std::string> listed;
    for (const char* name : names) {
        if (!listed.insert(name).second) {
            std::printf("  listed twice: %s\n", name);
            ++g_failures;
        }
        const std::filesystem::path path = "assets/sounds/" + folder + "/" + name + ".wav";
        if (!is_wav(path)) {
            std::printf("  missing or not a WAV: %s\n", path.string().c_str());
            ++g_failures;
        }
    }
    for (const auto& entry : std::filesystem::directory_iterator("assets/sounds/" + folder))
        if (entry.path().extension() == ".wav" && !listed.contains(entry.path().stem().string())) {
            std::printf("  not in src/audio/sounds.h: %s\n", entry.path().string().c_str());
            ++g_failures;
        }
}

// The shuffle: each mood deals only its own tracks, every one once before
// any repeats, and never the same one twice in a row.
void check_playlist() {
    for (std::uint32_t seed = 0; seed < 50; ++seed) {
        audio::Playlist playlist(seed);
        for (const audio::MusicMood mood : {audio::MusicMood::Calm, audio::MusicMood::Combat}) {
            std::set<audio::Music> all;
            for (int i = 0; i < audio::kMusicCount; ++i)
                if (audio::music_mood(static_cast<audio::Music>(i)) == mood)
                    all.insert(static_cast<audio::Music>(i));
            assert(all.size() >= 2);
            std::optional<audio::Music> last;
            for (int round = 0; round < 5; ++round) {
                std::set<audio::Music> dealt;
                for (std::size_t k = 0; k < all.size(); ++k) {
                    const audio::Music m = playlist.Next(mood);
                    assert(audio::music_mood(m) == mood);
                    assert(dealt.insert(m).second);
                    assert(last != m);
                    last = m;
                }
                assert(dealt == all);
            }
        }
    }
    for (int i = 0; i < audio::kMusicCount; ++i)
        assert(audio::music_plays(static_cast<audio::Music>(i)) >= 1);
}

}  // namespace

int main() {
    check_folder("sfx", audio::kSoundNames);
    check_folder("music", audio::kMusicNames);
    assert(std::string(audio::sound_name(audio::Sound::UiClick)) == "ui_click");
    assert(std::string(audio::music_name(audio::Music::ColdOrbit)) == "cold_orbit");
    assert(audio::music_mood(audio::Music::RedAlert) == audio::MusicMood::Combat);
    check_playlist();
    if (g_failures > 0)
        std::printf("test_sounds: %d sounds missing or unlisted (python3 tools/make_sounds.py makes them)\n",
                    g_failures);
    assert(g_failures == 0);
    std::puts("test_sounds: all passed");
    return 0;
}
