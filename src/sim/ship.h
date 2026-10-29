// ship.h — Ship: the square build grid, its hull and the buildings on it,
// and the placement rules.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// Rules: the hull is the ship's layout's (see ship_layout.h), fixed for good,
// as are the bridge and the ion thrusters it comes with. A building needs
// hull under every cell of its footprint, and all of them free; each
// placement takes one item from the bridge's inventory, and each removal
// gives it back.
//
// Storage: grids indexed [y][x]. A building lives in `buildings` at the
// top-left cell of its footprint (that index is its position); every cell
// it covers records that top-left cell in `anchor`, and `placed` lists the
// positions of all of them, oldest first.
//
// Item flow (Ship::tick): belts carry items to whatever they point at, and
// buildings hand their output to the neighbors that will take it, belts
// included (see insert_item). Filter and reverse sorters pass each item
// straight on to a neighbor in the same step; junctions and routers hold
// items a moment, then pass them on.
#pragma once

#include "sim/building.h"
#include "sim/game_types.h"
#include "sim/sector.h"
#include "sim/ship_layout.h"

#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

// Side of the square build grid, in cells: room for the largest layout.
constexpr int kShipGridSize = 128;

// Why a place() or remove_*() call did (or didn't) change the ship.
enum class BuildResult {
    Ok,
    OutOfBounds,
    NoHull,            // a building needs hull under its whole footprint
    Occupied,          // another building covers one of those cells
    NoneInInventory,
    NothingThere,      // no building to remove on that cell
    NotRemovable,      // the bridge or an engine: part of the layout
};

// A short player-facing explanation ("Needs hull under every tile", ...).
const char* describe(BuildResult result);

// Placements not made yet, which a batch of them (a blueprint paste) checks
// each later one against: the cells to be built on, and the inventory as it
// will be by then. Cells are keyed by cell_key().
struct PlannedChanges {
    std::unordered_set<int> occupied;
    Inventory inventory;

    static constexpr int cell_key(GridPos p) { return p.y * kShipGridSize + p.x; }
};

class Ship {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    template <typename T>
    using Grid = std::vector<std::vector<T>>;

    const ShipLayout* layout = nullptr;          // null only for an empty Ship()
    Grid<char> hull;                             // per cell: 1 for hull (char, not vector<bool>)
    Grid<std::unique_ptr<Building>> buildings;  // at each footprint's top-left cell, else null
    Grid<std::optional<GridPos>> anchor;         // per cell: top-left of the building covering it
    std::vector<GridPos> placed;                 // every building's top-left cell, oldest first
    Bridge* bridge = nullptr;                    // owned by `buildings`
    // Where the ship is: the sector's yields, which its collectors gather
    // at. Not saved: the World sets them from the sector.
    Yields yields = [] {
        Yields all;
        all.fill(100);
        return all;
    }();

    Building* building_at_mut(GridPos p);
    // Offers one item from `b`'s output buffer to its neighbors, round
    // robin; moves it to the first that takes it.
    void push_output(GridPos p, Building& b);
    // For a junction or router at `p`: hands on the items that are ready to
    // go (one per way across a junction; a router's one, to the next
    // neighbor in turn that takes it).
    void pass_on(GridPos p, Building& b);
    // insert_item(), for an item that left `source` (the building it was
    // output from, before any sorters): a belt that points into `source`
    // won't take it, as it would only carry it straight back.
    bool insert_item(GridPos to, GridPos from, const Building* source, ItemKind kind);
    // insert_item() for the sorter at `at`: hands the item on, from `from`,
    // to the neighbor its filter sends it to.
    bool sort_item(GridPos at, GridPos from, const Building* source, Sorter& sorter, ItemKind kind);

    // Puts a building at `p` (its top-left cell) and marks its footprint,
    // which must be free.
    void add_building(GridPos p, std::unique_ptr<Building> building);
    // Sets a collector's yield from `yields`, for its target.
    void update_yield(Collector& collector) const;
    // Takes the building at `anchor` (its top-left cell) off the grid,
    // returning it.
    std::unique_ptr<Building> take_building(GridPos anchor);
    // Sets the hull to `ship_layout`'s, centered on the grid, without its
    // bridge or engines (loading adds them from the save).
    void set_hull(const ShipLayout& ship_layout);

public:
    // An empty grid (no hull, no bridge). Games start from new_game().
    Ship();

    // A ship in `ship_layout`: its hull, with its bridge and engines in
    // place, and nothing in the inventory.
    static Ship from_layout(const ShipLayout& ship_layout);
    // The starting ship: the starter layout, with the bridge inventory
    // stocked.
    static Ship new_game();

    int size() const { return kShipGridSize; }
    bool in_bounds(GridPos p) const;
    bool has_hull(GridPos p) const;
    // The layout it was built in (null only for an empty Ship()).
    const ShipLayout* get_layout() const { return layout; }
    // The top-left cell of the building covering `p`, if any.
    std::optional<GridPos> anchor_of(GridPos p) const;
    // The building covering `p` (any cell of its footprint), or null.
    const Building* building_at(GridPos p) const;
    // The building whose top-left cell is `p`, or null (also null on the
    // other cells of a larger building).
    const Building* building_anchored_at(GridPos p) const;
    // The same, to change it (combat).
    Building* building_anchored_at(GridPos p);
    // Every building's top-left cell, oldest first (the order they tick in).
    const std::vector<GridPos>& placement_order() const { return placed; }
    // How many cells have hull.
    int hull_count() const;
    // The smallest box holding all the hull: its top-left cell and size.
    std::pair<GridPos, GridSize> hull_bounds() const;
    // Every ion thruster (engine) on board.
    std::vector<const Thruster*> thrusters() const;
    // Burns every thruster's fuel (a jump).
    void burn_thrusters();
    // Whether a building of kind `kind` is on board.
    bool has_building(ItemKind kind) const;

    // The yields of the sector the ship is in: its collectors gather at
    // them from now on (100 each, as at home, until set).
    void set_yields(const Yields& sector_yields);
    const Yields& get_yields() const { return yields; }

    Bridge& get_bridge() { return *bridge; }
    const Bridge& get_bridge() const { return *bridge; }
    Inventory& inventory() { return bridge->get_inventory(); }
    const Inventory& inventory() const { return bridge->get_inventory(); }

    // Whether place() would succeed, without changing anything.
    BuildResult can_place(ItemKind kind, GridPos p, Direction facing = Direction::North) const;
    // can_place() as if the `planned` changes had been made first, with
    // their inventory instead of the bridge's.
    BuildResult can_place(ItemKind kind, GridPos p, Direction facing, const PlannedChanges& planned) const;
    // Places one building, taking it from the inventory. `p` is the
    // top-left cell of its footprint. `facing` is used by rotatable
    // buildings only (and turns a non-square one's footprint).
    BuildResult place(ItemKind kind, GridPos p, Direction facing = Direction::North);
    // Removes the building covering `p` (whole footprint), refunding it.
    BuildResult remove_building(GridPos p);

    // Combat: the building at `anchor` (its top-left cell) is destroyed:
    // off the grid, and nothing comes back, neither it nor what it held.
    // Not the layout's parts (the bridge, whose loss is the ship's, see
    // bridge_destroyed; and the engines, knocked offline instead).
    void destroy_building(GridPos anchor);
    bool bridge_destroyed() const { return bridge->get_hp() == 0; }
    // Mends every building by `percent` of its max_hp (at least 1 each).
    void repair_all(int percent);

    // Hands one item to the building covering `to`, coming from the next
    // cell over, `from`; false (and no change) if it won't take it. A belt
    // takes items from behind or from the side, never from the cell it
    // points at: from behind they start at its back edge, from the side
    // halfway along, unless no belt feeds it from behind (a corner), which
    // counts as behind; nor, from a sorter, if it points into the building
    // the item came from before the sorter. A sorter takes the item only if
    // the neighbor it sends it to takes it now; an item from one sorter
    // never goes through a second straight into a third (nor round a ring
    // of them). A junction takes it while its queue that way has room and
    // something stands on the far side; a router while it's empty.
    bool insert_item(GridPos to, GridPos from, ItemKind kind);
    // Sets the recipe of the machine covering `p` (null: none), refunding
    // the ingredients it held to the inventory; false if there's no machine
    // there, it can't run `recipe` or `recipe` isn't unlocked yet.
    bool set_recipe(GridPos p, const Recipe* recipe);
    // Sets the filter of the sorter at `p` (nullopt: none, every item turns);
    // false if there's no sorter there.
    bool set_sort_filter(GridPos p, std::optional<ItemKind> item);
    // Aims the collector at `p` at `target`; false if there's no collector
    // there or it can't gather that.
    bool set_collector_target(GridPos p, ItemKind target);

    // One simulation step: every building ticks (gathering, crafting, belts
    // moving), then junctions and routers pass on what's ready, then each
    // belt's front item goes to what it points at, then each building puts
    // one item of output out to a neighbor.
    void tick();
};
