// ship.h — Ship: the square build grid, its hull tiles and the buildings on
// them, and the placement rules.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// Rules: hull goes on any empty cell, and reinforced hull also on plain hull
// (an upgrade: the plain tile goes back to the inventory); a building needs
// hull under every cell of its footprint (reinforced hull for the large
// late-game ones, see needs_reinforced_hull), and all of them free; each
// placement takes one item from the bridge's inventory, and each removal
// gives it back. Buildings and hull come off separately: hull can only go
// once nothing stands on it.
//
// Storage: grids indexed [y][x]. A building lives in `buildings` at the
// top-left cell of its footprint (that index is its position); every cell
// it covers records that top-left cell in `anchor`, and `placed` lists the
// positions of all of them, oldest first.
//
// Item flow (Ship::tick): belts carry items to whatever they point at, and
// buildings hand their output to the neighbors that will take it, belts
// included (see insert_item). Filter sorters pass each item straight on to a
// neighbor in the same step.
#pragma once

#include "sim/building.h"
#include "sim/game_types.h"

#include <memory>
#include <optional>
#include <vector>

// Side of the square build grid, in cells. The starting ship is a small
// patch in its middle: the rest is room to grow.
constexpr int kShipGridSize = 128;

// Why a place() or remove_*() call did (or didn't) change the ship.
enum class BuildResult {
    Ok,
    OutOfBounds,
    NoHull,            // a building needs hull under its whole footprint
    NeedsReinforcedHull,
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

    Grid<std::optional<ItemKind>> hull;          // Hull or ReinforcedHull, per cell
    Grid<std::unique_ptr<Building>> buildings;  // at each footprint's top-left cell, else null
    Grid<std::optional<GridPos>> anchor;         // per cell: top-left of the building covering it
    std::vector<GridPos> placed;                 // every building's top-left cell, oldest first
    Bridge* bridge = nullptr;                    // owned by `buildings`

    Building* building_at_mut(GridPos p);
    // Offers one item from `b`'s output buffer to its neighbors, round
    // robin; moves it to the first that takes it.
    void push_output(GridPos p, Building& b);
    // insert_item() for the sorter at `at`: hands the item on, from `from`,
    // to the neighbor its filter sends it to.
    bool sort_item(GridPos at, GridPos from, Sorter& sorter, ItemKind kind);

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
    // Which hull is at `p` (Hull or ReinforcedHull), if any.
    std::optional<ItemKind> hull_at(GridPos p) const;
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
    BuildResult can_place(ItemKind kind, GridPos p, Direction facing = Direction::North) const;
    // Places one hull tile or building, taking it from the inventory. `p` is
    // the top-left cell of the building's footprint. `facing` is used by
    // rotatable buildings only (and turns a non-square one's footprint).
    BuildResult place(ItemKind kind, GridPos p, Direction facing = Direction::North);
    // Removes the building covering `p` (whole footprint), refunding it. The
    // hull under it stays.
    BuildResult remove_building(GridPos p);
    // Removes the hull tile at `p`, refunding it (plain or reinforced); only
    // once no building covers it.
    BuildResult remove_hull(GridPos p);

    // Hands one item to the building covering `to`, coming from the next
    // cell over, `from`; false (and no change) if it won't take it. A belt
    // takes items from behind or from the side, never from the cell it
    // points at: from behind they start at its back edge, from the side
    // halfway along, unless no belt feeds it from behind (a corner), which
    // counts as behind. A sorter takes the item only if the neighbor it
    // sends it to takes it now; an item from one sorter never goes through a
    // second straight into a third (nor round a ring of them).
    bool insert_item(GridPos to, GridPos from, ItemKind kind);
    // Sets the recipe of the machine covering `p` (null: none), refunding
    // the ingredients it held to the inventory; false if there's no machine
    // there or it can't run `recipe`.
    bool set_recipe(GridPos p, const Recipe* recipe);
    // Sets the filter of the sorter at `p` (nullopt: none, every item turns);
    // false if there's no sorter there.
    bool set_sort_filter(GridPos p, std::optional<ItemKind> item);

    // One simulation step: every building ticks (gathering, crafting, belts
    // moving), then each belt's front item goes to what it points at, then
    // each building puts one item of output out to a neighbor.
    void tick();
};
