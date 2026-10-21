// rng.h — Rng: the simulation's random numbers (splitmix64), small enough to
// save as a single number, so a loaded game rolls exactly what the original
// would have.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
#pragma once

#include <cstdint>

struct Rng {
    std::uint64_t state = 0;

    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // A whole number from `lo` to `hi`, both included (hi >= lo).
    int range(int lo, int hi) {
        const auto span = static_cast<std::uint64_t>(hi - lo) + 1;
        return lo + static_cast<int>(next() % span);
    }

    // A number from 0 up to (not including) 1.
    float unit() { return static_cast<float>(next() >> 40) / static_cast<float>(1ull << 24); }

    // True with probability `p`.
    bool chance(float p) { return unit() < p; }
};
