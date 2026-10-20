// game_types.h — pure simulation layer: shared primitive types and constants
// (time).
//
// Simulation headers must NOT include imgui/SDL/OpenGL headers: the game rules
// are completely independent of rendering, so the simulation can be tested or
// run headless without a window.
#pragma once

#include <cstdint>

using Tick = std::uint64_t;

constexpr Tick kTicksPerSecond = 60;
