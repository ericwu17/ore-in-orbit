// game_types.h — pure simulation layer: shared primitive types and constants
// (time, grid positions and sizes, directions, item kinds).
//
// Simulation headers must NOT include imgui/SDL/OpenGL headers: the game rules
// are completely independent of rendering, so the simulation can be tested or
// run headless without a window.
#pragma once

#include <cstdint>

using Tick = std::uint64_t;

constexpr Tick kTicksPerSecond = 60;

// A cell on the ship's square build grid: x grows right, y grows down.
struct GridPos {
    int x = 0;
    int y = 0;

    bool operator==(const GridPos&) const = default;
};

// How many cells a building covers, from its top-left cell (its GridPos).
struct GridSize {
    int w = 1;
    int h = 1;

    bool operator==(const GridSize&) const = default;
};

// Which way an oriented building (a transport belt) points.
enum class Direction {
    North,
    East,
    South,
    West,
};

// The next direction clockwise (North -> East -> South -> West -> North).
constexpr Direction rotated_cw(Direction d) {
    return static_cast<Direction>((static_cast<int>(d) + 1) % 4);
}

// Everything the bridge inventory can hold and the player can place: hull
// tiles plus one entry per placeable building. (The bridge itself is not an
// item: it is placed once, at the start of a game.)
enum class ItemKind {
    Hull,
    HydrogenCollector,
    AsteroidCollector,
    Pipe,
    TransportBelt,
    Count,  // not a kind: the number of kinds
};

constexpr int kItemKindCount = static_cast<int>(ItemKind::Count);

// The player-facing name of an item ("Hull", "Pipe", ...).
const char* item_name(ItemKind kind);
