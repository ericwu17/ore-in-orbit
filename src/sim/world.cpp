// world.cpp — the placeholder simulation (see world.h).
#include "sim/world.h"

#include <cmath>

namespace {

// Per-tick odds, in thousandths, so the rolls depend only on mt19937's output
// (which the standard fixes) and not on a library's distribution code.
constexpr std::uint32_t kArrivalPerMille = 12;
constexpr std::uint32_t kProcessPerMille = 10;

// Arrivals swing between quiet and busy over each in-game day, so the charts
// show a shape rather than flat noise.
std::uint32_t arrival_per_mille(Tick tick) {
    const double phase = static_cast<double>(tick % kTicksPerDay) / static_cast<double>(kTicksPerDay);
    const double swing = 0.5 + 0.5 * std::sin(phase * 2.0 * 3.14159265358979);
    return static_cast<std::uint32_t>(std::lround(kArrivalPerMille * (0.25 + 1.5 * swing)));
}

}  // namespace

void World::advance_tick() {
    if (rng() % 1000 < arrival_per_mille(current_tick)) {
        ++queue;
        stats[GlobalStat::Arrived].add(current_tick, 1);
    }
    if (queue > 0 && rng() % 1000 < kProcessPerMille) {
        --queue;
        stats[GlobalStat::Processed].add(current_tick, 1);
    }
    stats[GlobalStat::Queue].sample(current_tick, queue);
    ++current_tick;
}
