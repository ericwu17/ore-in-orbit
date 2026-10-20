// game_types.h — pure simulation layer: shared primitive types and constants
// (time, grid positions and sizes, directions, item and fluid kinds).
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

// Everything the bridge inventory (or a belt) can hold. The placeable kinds
// come first: hull tiles plus one entry per building (the bridge itself is not
// an item: it is placed once, at the start of a game). After them come the
// asteroid chunks, the raw resources crushed out of them, and what is made
// from those. Fluids are not items: see FluidKind.
enum class ItemKind {
    // Placeable: floor.
    Hull,
    ReinforcedHull,
    // Placeable: logistics.
    TransportBelt,
    ExpressBelt,
    BeltTunnel,
    FilterSorter,
    ReverseSorter,
    Junction,
    Router,
    CargoHold,
    Pipe,
    Pump,
    FluidTank,
    // Placeable: collectors.
    AsteroidCollector,
    TractorArray,
    HydrogenCollector,
    // Placeable: machines.
    AsteroidCrusher,
    SolarFurnace,
    ArcFurnace,
    IceMelter,
    Electrolyzer,
    ChemicalPlant,
    FabricatorMk1,
    FabricatorMk2,
    FabricatorMk3,
    // Placeable: power.
    SolarPanel,
    SolarArray,
    FuelCellGenerator,
    BatteryBank,
    // Placeable: ship systems.
    SurveyScanner,
    IonThruster,
    // Asteroid chunks (what the asteroid collector catches).
    MetallicChunk,
    SilicateChunk,
    CarbonaceousChunk,
    IceChunk,
    ExoticChunk,
    // Raw resources (what the crusher gets out of a chunk).
    IronOre,
    NickelOre,
    CopperOre,
    Silica,
    Carbon,
    WaterIce,
    IridiumOre,
    // Smelted and processed materials.
    IronPlate,
    CopperPlate,
    NickelPlate,
    Glass,
    Steel,
    Silicon,
    IridiumPlate,
    Plastic,
    Battery,
    PropellantCell,
    // Components.
    Gear,
    CopperWire,
    Circuit,
    ServoMotor,
    StructuralFrame,
    PhotovoltaicCell,
    AdvancedCircuit,
    Processor,
    Count,  // not a kind: the number of kinds
};

constexpr int kItemKindCount = static_cast<int>(ItemKind::Count);
// The asteroid chunks, MetallicChunk to ExoticChunk, are contiguous.
constexpr int kChunkKindCount = static_cast<int>(ItemKind::IronOre) - static_cast<int>(ItemKind::MetallicChunk);

// The groups ItemKind is laid out in, in that order.
enum class ItemCategory {
    Placeable,
    AsteroidChunk,
    RawResource,
    Material,
    Component,
};

ItemCategory item_category(ItemKind kind);
// Hull tiles and buildings: what the player can put on the grid.
constexpr bool is_placeable(ItemKind kind) {
    return static_cast<int>(kind) <= static_cast<int>(ItemKind::IonThruster);
}

// The player-facing name of an item ("Hull", "Iron plate", ...).
const char* item_name(ItemKind kind);
// An item's stable id: its enumerator's name ("Hull", "IronPlate", ...).
// Saves write items by it, and sprite files are named after it.
const char* item_id(ItemKind kind);

// What pipes carry. Collectors, melters and electrolyzers make them;
// chemical plants and generators use them up.
enum class FluidKind {
    Water,
    Hydrogen,
    Oxygen,
    Count,  // not a kind: the number of kinds
};

constexpr int kFluidKindCount = static_cast<int>(FluidKind::Count);

const char* fluid_name(FluidKind kind);
// A fluid's stable id: its enumerator's name (sprite files are named after it).
const char* fluid_id(FluidKind kind);
