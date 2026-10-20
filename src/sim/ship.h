// ship.h — Ship: the square build grid, its hull tiles and the buildings on
// them, and the placement rules.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// Rules: hull goes on any empty cell; a building needs hull under every cell
// of its footprint, and all of them free; each placement takes one item from
// the bridge's inventory, and each removal gives it back. Buildings and hull
// come off separately: hull can only go once nothing stands on it.
//
// Storage: grids indexed [y][x]. A building lives in `buildings` at the
// top-left cell of its footprint (that index is its position); every cell
// it covers records that top-left cell in `anchor`.
#pragma once

#include "sim/building.h"
#include "sim/game_types.h"

#include <memory>
#include <optional>
#include <vector>

constexpr int kShipGridSize = 32;

// Why a place() or remove_*() call did (or didn't) change the ship.
enum class BuildResult {
    Ok,
    OutOfBounds,
    NoHull,            // a building needs hull under its whole footprint
    HullAlreadyThere,
    Occupied,          // another building covers one of those cells
    NoneInInventory,
    NothingThere,      // nothing of that layer to remove on that cell
    NotRemovable,      // the bridge
    HullNotEmpty,      // remove_hull() under a building
};

// A short player-facing explanation ("Needs hull under every tile", ...).
const char* describe(BuildResult result);

class Ship {
private:
    template <typename T>
    using Grid = std::vector<std::vector<T>>;

    Grid<bool> hull;
    Grid<std::unique_ptr<Building>> buildings;  // at each footprint's top-left cell, else null
    Grid<std::optional<GridPos>> anchor;         // per cell: top-left of the building covering it
    Bridge* bridge = nullptr;                    // owned by `buildings`

    // Puts a building at `p` (its top-left cell) and marks its footprint,
    // which must be free.
    void add_building(GridPos p, std::unique_ptr<Building> building);

public:
    // An empty grid (no hull, no bridge). Games start from new_game().
    Ship();

    // The starting ship: a small hull patch with the bridge in the middle,
    // and the bridge inventory stocked.
    static Ship new_game();

    int size() const { return kShipGridSize; }
    bool in_bounds(GridPos p) const;
    bool has_hull(GridPos p) const;
    // The top-left cell of the building covering `p`, if any.
    std::optional<GridPos> anchor_of(GridPos p) const;
    // The building covering `p` (any cell of its footprint), or null.
    const Building* building_at(GridPos p) const;
    // The building whose top-left cell is `p`, or null (also null on the
    // other cells of a larger building).
    const Building* building_anchored_at(GridPos p) const;

    Bridge& get_bridge() { return *bridge; }
    const Bridge& get_bridge() const { return *bridge; }
    Inventory& inventory() { return bridge->get_inventory(); }
    const Inventory& inventory() const { return bridge->get_inventory(); }

    // Whether place() would succeed, without changing anything.
    BuildResult can_place(ItemKind kind, GridPos p) const;
    // Places one hull tile or building, taking it from the inventory. `p` is
    // the top-left cell of the building's footprint. `facing` is used by
    // rotatable buildings only.
    BuildResult place(ItemKind kind, GridPos p, Direction facing = Direction::North);
    // Removes the building covering `p` (whole footprint), refunding it. The
    // hull under it stays.
    BuildResult remove_building(GridPos p);
    // Removes the hull tile at `p`, refunding it; only once no building
    // covers it.
    BuildResult remove_hull(GridPos p);

    // One simulation step for every building.
    void tick();
};
