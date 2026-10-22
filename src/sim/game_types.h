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

// The next direction counterclockwise (North -> West -> South -> East).
constexpr Direction rotated_ccw(Direction d) {
    return static_cast<Direction>((static_cast<int>(d) + 3) % 4);
}

// The cell next to `p` in direction `d`.
constexpr GridPos neighbor(GridPos p, Direction d) {
    switch (d) {
    case Direction::North: return {p.x, p.y - 1};
    case Direction::East:  return {p.x + 1, p.y};
    case Direction::South: return {p.x, p.y + 1};
    case Direction::West:  return {p.x - 1, p.y};
    }
    return p;
}

// The direction of the step from `from` to `to`, which must be next to it.
constexpr Direction direction_to(GridPos from, GridPos to) {
    if (to.y < from.y)
        return Direction::North;
    if (to.x > from.x)
        return Direction::East;
    if (to.y > from.y)
        return Direction::South;
    return Direction::West;
}

// Everything the bridge inventory (or a belt) can hold. The building kinds
// come first, one entry per building (the bridge itself is not one: it comes
// with the ship's layout, as do its ion thrusters, which have a kind but are
// never items: see is_layout_part). After them come the raw resources the
// collectors gather, and what is made from those.
enum class ItemKind {
    // Placeable: logistics.
    TransportBelt,
    ExpressBelt,
    BeltTunnel,
    FilterSorter,
    ReverseSorter,
    Junction,
    Router,
    CargoHold,
    // Placeable: collectors.
    AsteroidCollector,
    TractorArray,
    // Placeable: machines.
    SolarFurnace,
    Refinery,
    FabricatorMk1,
    FabricatorMk2,
    FabricatorMk3,
    // Placeable: power.
    SolarPanel,
    SolarArray,
    BatteryBank,
    // Placeable: ship systems.
    SurveyScanner,
    IonThruster,
    // Placeable: turrets.
    Autocannon,
    Railgun,
    // Raw resources (what the collectors gather).
    IronOre,
    CopperOre,
    Silica,
    Carbon,
    IridiumOre,
    // Smelted and processed materials.
    IronPlate,
    CopperPlate,
    Glass,
    Steel,
    Silicon,
    IridiumPlate,
    Plastic,
    FuelCell,
    // Components.
    Gear,
    Circuit,
    StructuralFrame,
    AdvancedCircuit,
    Processor,
    // Fleet: launching ships and moving cargo between them.
    ShipStarterPack,
    CargoPod,
    // Ammunition, for the turrets.
    SlugMagazine,
    RailSlug,
    Count,  // not a kind: the number of kinds
};

constexpr int kItemKindCount = static_cast<int>(ItemKind::Count);

// The groups ItemKind is laid out in, in that order.
enum class ItemCategory {
    Placeable,
    RawResource,
    Material,
    Component,
};

ItemCategory item_category(ItemKind kind);
// Every kind of building, the layout parts included.
constexpr bool is_building(ItemKind kind) {
    return static_cast<int>(kind) <= static_cast<int>(ItemKind::Railgun);
}
// Buildings that come only with a ship's layout (its engines): never in an
// inventory, never placed or removed by the player.
constexpr bool is_layout_part(ItemKind kind) {
    return kind == ItemKind::IonThruster;
}
// The buildings the player can put on the grid.
constexpr bool is_placeable(ItemKind kind) {
    return is_building(kind) && !is_layout_part(kind);
}

// The player-facing name of an item ("Transport belt", "Iron plate", ...).
const char* item_name(ItemKind kind);
// An item's stable id: its enumerator's name ("TransportBelt", "IronPlate", ...).
// Saves write items by it, and sprite files are named after it.
const char* item_id(ItemKind kind);
