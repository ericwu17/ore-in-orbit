// test_sounds.cpp — headless checks of assets/sounds/ (see
// src/audio/sounds.h): every Sound and Music has its WAV file, and every
// file there is one of them, so the list and tools/make_sounds.py can't
// drift apart. Runs from the repo root, like every test.
#include "audio/sounds.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

}  // namespace

int main() {
    check_folder("sfx", audio::kSoundNames);
    check_folder("music", audio::kMusicNames);
    assert(std::string(audio::sound_name(audio::Sound::UiClick)) == "ui_click");
    assert(std::string(audio::music_name(audio::Music::ColdOrbit)) == "cold_orbit");
    if (g_failures > 0)
        std::printf("test_sounds: %d sounds missing or unlisted (python3 tools/make_sounds.py makes them)\n",
                    g_failures);
    assert(g_failures == 0);
    std::puts("test_sounds: all passed");
    return 0;
}
