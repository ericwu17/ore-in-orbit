// playlist.h — which music track plays next: a shuffle of each mood's
// tracks (sounds.h). Header-only and free of SDL, like sounds.h, so the
// headless tests can check it (tests/test_sounds.cpp).
#pragma once

#include "audio/sounds.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

namespace audio {

// Deals each mood's tracks like a shuffled deck: every one plays once
// before any plays again, and a new deck never starts with the track that
// ended the last, so nothing plays twice in a row.
class Playlist {
public:
    Playlist() = default;
    explicit Playlist(std::uint32_t seed) : rng_(seed) {}

    // The next track of `mood`.
    Music Next(MusicMood mood) {
        const std::size_t m = static_cast<std::size_t>(mood);
        std::vector<Music>& deck = decks_[m];
        if (deck.empty()) {
            for (int i = 0; i < kMusicCount; ++i)
                if (kMusicMoods[static_cast<std::size_t>(i)] == mood)
                    deck.push_back(static_cast<Music>(i));
            std::shuffle(deck.begin(), deck.end(), rng_);
            // Dealt from the back: keep the last one played off the top.
            if (deck.size() > 1 && last_[m] == deck.back())
                std::swap(deck.back(), deck.front());
        }
        const Music next = deck.back();
        deck.pop_back();
        last_[m] = next;
        return next;
    }

private:
    std::mt19937 rng_;
    std::array<std::vector<Music>, kMusicMoodCount> decks_;
    std::array<std::optional<Music>, kMusicMoodCount> last_;
};

}  // namespace audio
