// world.h — World: owns all simulation state and drives it tick by tick.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// A placeholder simulation, there so the frame loop, transport bar and
// statistics window have something to run and chart: items arrive in a queue
// at random and are processed one at a time. Replace it with your own.
#pragma once

#include "sim/game_types.h"
#include "sim/stats.h"

#include <cstdint>
#include <random>

// The seed a World's RNG starts from.
inline constexpr std::uint32_t kDefaultSeed = 20260926u;

class World {
private:
    Tick current_tick = 0;
    bool paused = false;
    float speed_multiplier = 1.0f;  // 1 = real time; >1 = fast-forward (transport bar)

    // Statistics (see stats.h). Simulation state like any other.
    GlobalStats stats;

    // The example simulation's only state.
    int64_t queue = 0;

    std::mt19937 rng{kDefaultSeed};  // fixed seed -> fully deterministic simulation

public:
    // One fixed simulation step (kTicksPerSecond of them per real second at 1x).
    void advance_tick();

    void set_paused(bool p) { paused = p; }
    void set_speed_multiplier(float m) { speed_multiplier = m; }

    Tick get_tick() const { return current_tick; }
    bool is_paused() const { return paused; }
    float get_speed_multiplier() const { return speed_multiplier; }
    int64_t get_queue() const { return queue; }
    const GlobalStats& get_stats() const { return stats; }
};
