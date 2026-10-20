// building.h — everything that stands on a hull tile, as a class hierarchy,
// plus the bridge's Inventory and hand-crafting queue.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
//   Building (abstract)
//   ├── Bridge       the ship's command tile; owns the inventory, hand-crafts
//   ├── Collector    gathers from space: asteroid collector, tractor array,
//   │                hydrogen collector
//   ├── Conveyor     moves items along: belts (belt tunnel and splitter don't
//   │                work yet)
//   ├── Sorter       routes items by kind (the filter sorter)
//   ├── CargoHold    item storage
//   ├── FluidPart    pipe, pump, fluid tank
//   ├── Machine      runs recipes: crusher, furnaces, ice melter, electrolyzer,
//   │                chemical plant, fabricators
//   ├── PowerPlant   solar panel / array, fuel-cell generator, battery bank
//   └── ShipSystem   survey scanner, ion thruster
//
// One class per family, not per building: each building's footprint and
// whether it rotates is data (footprint_of, is_rotatable), looked up by item
// kind before one exists. A building doesn't know where it is: the Ship
// stores it at its footprint's top-left cell.
//
// There are no inserters: as in Mindustry, belts feed straight into the
// building they point at, and buildings put their output straight onto the
// belts (or into the buildings) next to them. A building takes items in
// through accepts() / receive() and offers what it makes from its
// output_buffer(); the Ship moves them between neighbors each tick (see
// Ship::tick). Pipes will do the same for fluids; they don't carry any yet.
#pragma once

#include "sim/game_types.h"
#include "sim/recipes.h"

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

// Counts of each item: the bridge's inventory, and the buffers inside
// buildings.
class Inventory {
private:
    std::array<int, kItemKindCount> counts{};

public:
    int count(ItemKind kind) const { return counts[static_cast<std::size_t>(kind)]; }
    // All items, of every kind.
    int total() const;
    // One stack per kind held, in ItemKind order.
    std::vector<ItemStack> stacks() const;
    void add(ItemKind kind, int n = 1);
    void add(const std::vector<ItemStack>& stacks);
    // Removes n; false (and no change) if there are fewer.
    bool take(ItemKind kind, int n = 1);
    bool has(const std::vector<ItemStack>& stacks) const;
    // Removes every stack; false (and no change) unless all are there.
    bool take(const std::vector<ItemStack>& stacks);
};

// The footprint a building of this kind will have when facing `facing`
// (1x1 for hull). Rotatable buildings that aren't square turn with it: a
// splitter is 2x1 facing north or south and 1x2 facing east or west.
GridSize footprint_of(ItemKind kind, Direction facing = Direction::North);
// Whether the player picks a direction when placing it (belts, pumps).
bool is_rotatable(ItemKind kind);
// The large late-game buildings that need reinforced hull under them.
bool needs_reinforced_hull(ItemKind kind);
// Whether pipes connect to it.
bool handles_fluid(ItemKind kind);

class Building {
private:
    std::optional<ItemKind> placed_by;
    Direction dir;
    GridSize size;

public:
    // `placed_by` is nullopt for buildings that never came from the inventory
    // (the bridge).
    Building(std::optional<ItemKind> k, Direction facing, GridSize s);
    virtual ~Building() = default;
    Building(const Building&) = delete;
    Building& operator=(const Building&) = delete;

    GridSize footprint() const { return size; }
    // The item that placed this building, refunded when it is removed.
    std::optional<ItemKind> item_kind() const { return placed_by; }
    Direction facing() const { return dir; }
    bool rotatable() const { return placed_by && is_rotatable(*placed_by); }
    virtual bool removable() const { return true; }
    // One simulation step.
    virtual void tick() {}
    // The items held inside it (buffers), refunded to the bridge when it is
    // removed.
    virtual std::vector<ItemStack> contents() const { return {}; }

    // Item input: whether it takes one `kind` from a neighbor now, and
    // taking it (only after accepts() said yes). Belts are the exception:
    // where an item comes from matters to them (see Ship::insert_item).
    virtual bool accepts(ItemKind) const { return false; }
    virtual void receive(ItemKind) {}
    // Item output: what it has made, waiting to go to its neighbors (any
    // kind in it, one item per tick); null if it makes nothing.
    virtual Inventory* output_buffer() { return nullptr; }
    // Which neighbor gets offered output next (round robin, kept by the Ship).
    std::size_t next_output_side = 0;
};

// A recipe the bridge is hand-crafting. Its inputs left the inventory when
// it was queued.
struct CraftJob {
    const Recipe* recipe = nullptr;
    Tick elapsed = 0;
};

// The bridge's footprint is kBridgeSize square.
constexpr int kBridgeSize = 6;

class Bridge : public Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    Inventory inventory;
    std::deque<CraftJob> queue;  // front is in progress

public:
    Bridge();

    bool removable() const override { return false; }
    // Advances the front craft job; a finished one puts its outputs in the
    // inventory.
    void tick() override;
    // The ship's core: belts deliver anything into the inventory.
    bool accepts(ItemKind) const override { return true; }
    void receive(ItemKind kind) override { inventory.add(kind); }

    Inventory& get_inventory() { return inventory; }
    const Inventory& get_inventory() const { return inventory; }

    // Whether queue_craft(recipe) would succeed now.
    bool can_hand_craft(const Recipe& recipe) const;
    // Takes the recipe's inputs from the inventory and queues it; false (and
    // no change) if it isn't hand-craftable or the inputs aren't there.
    bool queue_craft(const Recipe& recipe);
    // Drops queued job `index`, refunding its inputs; false if there is none.
    bool cancel_craft(std::size_t index);
    const std::deque<CraftJob>& craft_queue() const { return queue; }
};

// How a collector gathers: one catch every `interval` ticks into its
// buffer, which holds up to `capacity`; it stalls while the buffer is full.
// A catch is one asteroid chunk, picked by `chunk_weights` (indexed from
// MetallicChunk), or `fluid_amount` units of `fluid`.
struct CollectorSpec {
    Tick interval = 0;
    int capacity = 0;  // items, or fluid units
    std::array<int, kChunkKindCount> chunk_weights{};
    std::optional<FluidKind> fluid{};
    int fluid_amount = 0;
};

// The spec of a collector kind (asteroid collector, tractor array,
// hydrogen collector).
const CollectorSpec& collector_spec(ItemKind kind);

class Collector : public Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    const CollectorSpec* spec;
    Tick progress = 0;  // ticks toward the next catch
    Inventory caught;   // chunks
    int fluid = 0;      // units of spec->fluid
    // Smooth weighted round robin over the chunk kinds: spreads each kind's
    // share of the catches evenly, with no randomness.
    std::array<int, kChunkKindCount> chunk_turn{};

public:
    explicit Collector(ItemKind k);

    const CollectorSpec& get_spec() const { return *spec; }
    // Gathers, unless the buffer is full.
    void tick() override;
    std::vector<ItemStack> contents() const override { return caught.stacks(); }
    Inventory* output_buffer() override { return spec->fluid ? nullptr : &caught; }

    const Inventory& items() const { return caught; }
    Inventory& items() { return caught; }
    int fluid_amount() const { return fluid; }
    // How much the buffer holds (items or fluid units), out of capacity().
    int stored() const { return spec->fluid ? fluid : caught.total(); }
    int capacity() const { return spec->capacity; }
    bool full() const { return stored() >= capacity(); }
    // Items (or fluid units) gathered per second while not full.
    double rate_per_second() const;
    // How far along the next catch is, 0 to 1.
    float catch_progress() const;
};

// Positions along a belt run from 0 (its back edge) to kBeltLength (its
// front edge); items on it keep at least kBeltSpacing apart, so a full belt
// holds kBeltLength / kBeltSpacing + 1 of them.
constexpr int kBeltLength = 60;
constexpr int kBeltSpacing = 20;

// Belt speed in positions per tick (a cell is kBeltLength): 0 for the
// conveyors that don't move items yet (tunnel, splitter).
int belt_speed(ItemKind kind);

struct BeltItem {
    ItemKind kind;
    int pos = 0;
};

class Conveyor : public Building {
private:
    std::vector<BeltItem> items;  // front (highest pos) first

public:
    Conveyor(ItemKind k, Direction facing) : Building(k, facing, footprint_of(k, facing)) {}

    int speed() const { return belt_speed(*item_kind()); }
    bool moves_items() const { return speed() > 0; }
    // Moves every item forward, up to the front edge or the item ahead.
    void tick() override;
    std::vector<ItemStack> contents() const override;

    const std::vector<BeltItem>& get_items() const { return items; }
    // Whether an item fits at `pos` (kBeltSpacing clear on both sides).
    bool has_room_at(int pos) const;
    // Puts an item at `pos`; has_room_at(pos) must hold.
    void insert(ItemKind kind, int pos);
    // The front item, once it has reached the front edge.
    std::optional<ItemKind> item_at_front() const;
    void pop_front();
};

// The filter sorter, as in Mindustry: an item matching its filter carries
// straight on through, out the side opposite the one it came in by; any
// other item turns left or right, alternating when both sides would take it.
// It holds nothing: it takes an item only if where it would go takes it at
// once (see Ship::insert_item). With no filter, every item turns.
class Sorter : public Building {
private:
    std::optional<ItemKind> filter;

public:
    Sorter() : Building(ItemKind::FilterSorter, Direction::North, footprint_of(ItemKind::FilterSorter)) {}

    std::optional<ItemKind> get_filter() const { return filter; }
    void set_filter(std::optional<ItemKind> item) { filter = item; }
    // Which side an item that turns tries first: right of its travel
    // direction, or left. Kept by the Ship, which flips it after each turn.
    bool next_right = false;
};

// Storage: takes any item until it holds kCargoCapacity.
constexpr int kCargoCapacity = 100;

class CargoHold : public Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    Inventory stored;

public:
    CargoHold() : Building(ItemKind::CargoHold, Direction::North, footprint_of(ItemKind::CargoHold)) {}

    const Inventory& get_stored() const { return stored; }
    std::vector<ItemStack> contents() const override { return stored.stacks(); }
    bool accepts(ItemKind) const override { return stored.total() < kCargoCapacity; }
    void receive(ItemKind kind) override { stored.add(kind); }
};

class FluidPart : public Building {
public:
    FluidPart(ItemKind k, Direction facing) : Building(k, facing, footprint_of(k, facing)) {}
};

// A machine's buffers, in crafts of its recipe: it takes ingredients until
// it holds kInputCrafts crafts' worth, and stops crafting while a product
// has kOutputCrafts crafts' worth waiting to leave.
constexpr int kInputCrafts = 2;
constexpr int kOutputCrafts = 4;

// Runs the recipe the player picked (one of the categories it handles), at
// its crafting speed: ingredients come in from belts and neighbors, a craft
// starts once all of them (and its fluids) are there and there's room for
// the products, which then wait in the output buffer to go out.
class Machine : public Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    const Recipe* recipe = nullptr;
    Inventory input;
    Inventory output;
    std::array<int, kFluidKindCount> fluid_in{};
    std::array<int, kFluidKindCount> fluid_out{};
    std::optional<Tick> work;  // while crafting: progress, in ticks at speed 1

    bool can_start() const;

public:
    explicit Machine(ItemKind k) : Building(k, Direction::North, footprint_of(k)) {}

    bool crafts(CraftCategory category) const;
    bool can_run(const Recipe& r) const { return crafts(r.category); }
    // Recipe time multiplier: 2 runs a recipe in half its ticks.
    int crafting_speed() const;

    const Recipe* get_recipe() const { return recipe; }
    // Switches to `r` (null: none), which it must be able to run. Returns
    // the ingredients it was holding (delivered, and those of a craft in
    // progress, which is dropped), for the caller to refund. Products
    // already made stay in the output buffer.
    std::vector<ItemStack> set_recipe(const Recipe* r);

    void tick() override;
    std::vector<ItemStack> contents() const override;
    bool accepts(ItemKind kind) const override;
    void receive(ItemKind kind) override { input.add(kind); }
    Inventory* output_buffer() override { return &output; }

    const Inventory& get_input() const { return input; }
    const Inventory& get_output() const { return output; }
    int fluid_input(FluidKind f) const { return fluid_in[static_cast<std::size_t>(f)]; }
    int fluid_output(FluidKind f) const { return fluid_out[static_cast<std::size_t>(f)]; }
    bool crafting() const { return work.has_value(); }
    // How far along the current craft is, 0 to 1 (0 when idle).
    float craft_progress() const;
    // Crafts per minute while kept supplied (0 without a recipe).
    double crafts_per_minute() const;
};

// The recipes a machine of this kind can run, in all_recipes() order.
std::vector<const Recipe*> machine_recipes(ItemKind kind);

class PowerPlant : public Building {
public:
    explicit PowerPlant(ItemKind k) : Building(k, Direction::North, footprint_of(k)) {}
};

class ShipSystem : public Building {
public:
    explicit ShipSystem(ItemKind k) : Building(k, Direction::North, footprint_of(k)) {}
};

// Which building kinds handle a category (the machines; Assembly also
// includes hand-crafting at the bridge, which isn't listed).
bool machine_crafts(ItemKind kind, CraftCategory category);
int machine_crafting_speed(ItemKind kind);

// A new building of the given kind (any placeable kind but the hull ones,
// which are not buildings). `facing` only matters for rotatable kinds.
std::unique_ptr<Building> make_building(ItemKind kind, Direction facing);
