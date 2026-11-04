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
// Item flow (Ship::tick): tubes carry items from cell to cell and into the
// buildings next to them, by the rules in building.h (see Tube), and
// buildings hand their output to the neighbors that will take it, tubes
// included (see insert_item). Crossovers hold items a moment as they cross,
// then pass them on. A vacuum line carries items fast to its other end.
// Extractors take items out of the storage behind them.
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

// What a vacuum line placed somewhere would link to: the other end, if any,
// and whether the new one would be the exit.
struct VacuumPreview {
    std::optional<GridPos> partner;
    bool is_exit = false;
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
    // The cell of the nearest vacuum line facing `facing`, within
    // kVacuumRange of `p` going `way`, passing over anything else.
    std::optional<GridPos> vacuum_along(GridPos p, Direction way, Direction facing) const;
    // Links the vacuum lines up, from where they stand: walking each line
    // the way it runs, one not yet anyone's exit takes the nearest one
    // ahead, facing the same way and in range, as its exit (so a row of them
    // pairs off from the back). Called whenever one comes or goes.
    void link_vacuum_lines();
    // Offers one item from `b`'s output buffer to its neighbors, round
    // robin, overflow tubes only once none of the others takes it; moves it
    // to the first that takes it.
    void push_output(GridPos p, Building& b);
    // For the extractor at `p`, once it's ready: takes one item out of the
    // storage behind it into the cell in front, if that takes one (the
    // filter's kind, or the first kind in turn that it takes).
    void extract(GridPos p, Extractor& extractor);
    // For a crossover at `p`: hands on the items that are ready to go, one
    // per way across.
    void pass_on(GridPos p, Crossover& crossover);
    // For a vacuum line at `p`: an entrance's front item goes on to its exit
    // (with none, it stays), an exit's out of its front.
    void move_vacuum_item(GridPos p, VacuumLine& line);

    // How an item in tube pieces is routed (see Tube for the rules). Which
    // kind of way on side `side` of the tube at `p` is for an item of `kind`
    // in it: none; a building, or a filter tube that takes `kind` (rule 3:
    // tried first); a tube, crossover or vacuum line; or an overflow tube
    // (tried last).
    enum class TubeExit { None, Building, Tube, Overflow };
    TubeExit tube_exit(GridPos p, const Tube& tube, Direction side, ItemKind kind) const;
    // Whether the building covering `p` would take `kind` now (a filter
    // tube: whether it lets it in and has room).
    bool takes_now(GridPos p, ItemKind kind) const;
    // Whether the tube at `p` leads on to more tube (a tube piece,
    // crossover or vacuum line) for an item of `kind` that came in by side
    // `from`; where it doesn't, the tubes end and the item turns round.
    bool tube_leads_on(GridPos p, const Tube& tube, Direction from, ItemKind kind) const;
    // The side an item of `kind` coming into the tube at `p` by side `from`
    // heads for: a building that takes it now, or else the next tube way in
    // turn (moving the tube's turn on), or else an overflow tube; back the
    // way it came where the tubes end; straight across otherwise.
    Direction plan_tube_exit(GridPos p, Tube& tube, Direction from, ItemKind kind);
    // insert_item() into the tube at `to`.
    bool insert_into_tube(GridPos to, GridPos from, Tube& tube, ItemKind kind);
    // For the tube at `p`: its front item, once at the far side, leaves into
    // a building that takes it, else by the side it was headed for (or the
    // next way clockwise that is open), else into an overflow tube, else,
    // where the tubes end, back the way it came; or it waits.
    void move_tube_item(GridPos p, Tube& tube);

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
    // What a vacuum line facing `facing` would link to if placed at `p`.
    VacuumPreview vacuum_preview(GridPos p, Direction facing) const;
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
    // cell over, `from`; false (and no change) if it won't take it. A tube
    // takes it if it links to `from` (see tube_links), lets the item in and
    // has room, a check valve only from behind. A crossover takes it while
    // its queue that way has room and something stands on the far side. A
    // vacuum line's entrance takes items from any side but its front; its
    // exit takes none (its entrance hands them over).
    bool insert_item(GridPos to, GridPos from, ItemKind kind);
    // Whether the tube piece at `p` links to its neighbor on side `side`:
    // a tube piece of the same color (a check valve only by its front or
    // back, on either end), an extractor pointing at it, a vacuum line's
    // entrance from any side but its front or its exit by its front, or any
    // other building. False if there is no tube piece at `p`.
    bool tube_links(GridPos p, Direction side) const;
    // Sets the recipe of the machine covering `p` (null: none), refunding
    // the ingredients it held to the inventory; false if there's no machine
    // there, it can't run `recipe` or `recipe` isn't unlocked yet.
    bool set_recipe(GridPos p, const Recipe* recipe);
    // Sets the filter of the filter tube or extractor at `p` (nullopt: none:
    // a filter tube takes nothing, an extractor every kind); false if there's
    // neither there.
    bool set_filter(GridPos p, std::optional<ItemKind> item);
    // Sets the color of the tube piece at `p`; false if there's none there.
    bool set_tube_color(GridPos p, TubeColor color);
    // The storage (the bridge or a cargo hold) behind the extractor at `p`,
    // which it takes items out of; null if there's none, or no extractor.
    const Building* extract_source(GridPos p) const;
    // Aims the collector at `p` at `target`; false if there's no collector
    // there or it can't gather that.
    bool set_collector_target(GridPos p, ItemKind target);
    // Sets whether the collector or machine at `p` runs only while the
    // bridge requests its product; false if there's neither there.
    bool set_only_when_requested(GridPos p, bool on);

    // One simulation step: collectors and machines set to run only on
    // request go on standby (or off it) by the bridge's requests as they
    // stand, every building ticks (gathering, crafting, items moving
    // through tubes), then crossovers pass on what's ready, then each vacuum
    // line's front item goes on, then each tube's front item leaves for the
    // next cell or a building, then each building puts one item of output
    // out to a neighbor, and each ready extractor takes one out of storage.
    void tick();
};
