// building.h — everything that stands on a hull tile, as a class hierarchy,
// plus the bridge's Inventory and hand-crafting queue.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
//   Building (abstract)
//   ├── Bridge       the ship's command tile; owns the inventory, hand-crafts
//   ├── Collector    gathers from space: asteroid collector, tractor array,
//   │                hydrogen collector
//   ├── Conveyor     moves items along: belts, belt tunnel, splitter, filter sorter
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
// stores it at its footprint's top-left cell. Only the bridge does anything
// per tick: the bridge (hand-crafting) and the collectors (gathering).
//
// There are no inserters: as in Mindustry, belts and pipes feed straight
// into the building they point at, and buildings put their output straight
// onto the belts and pipes next to them.
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
    std::optional<ItemKind> kind;
    Direction dir;
    GridSize size;

public:
    // `kind` is nullopt for buildings that never came from the inventory
    // (the bridge).
    Building(std::optional<ItemKind> k, Direction facing, GridSize s);
    virtual ~Building() = default;
    Building(const Building&) = delete;
    Building& operator=(const Building&) = delete;

    GridSize footprint() const { return size; }
    // The item that placed this building, refunded when it is removed.
    std::optional<ItemKind> item_kind() const { return kind; }
    Direction facing() const { return dir; }
    bool rotatable() const { return kind && is_rotatable(*kind); }
    virtual bool removable() const { return true; }
    // One simulation step.
    virtual void tick() {}
    // The items held inside it (buffers), refunded to the bridge when it is
    // removed.
    virtual std::vector<ItemStack> contents() const { return {}; }
};

// A recipe the bridge is hand-crafting. Its inputs left the inventory when
// it was queued.
struct CraftJob {
    const Recipe* recipe = nullptr;
    Tick elapsed = 0;
};

class Bridge : public Building {
private:
    Inventory inventory;
    std::deque<CraftJob> queue;  // front is in progress

public:
    Bridge();

    bool removable() const override { return false; }
    // Advances the front craft job; a finished one puts its outputs in the
    // inventory.
    void tick() override;

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
    std::optional<FluidKind> fluid;
    int fluid_amount = 0;
};

// The spec of a collector kind (asteroid collector, tractor array,
// hydrogen collector).
const CollectorSpec& collector_spec(ItemKind kind);

class Collector : public Building {
private:
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

class Conveyor : public Building {
public:
    Conveyor(ItemKind k, Direction facing) : Building(k, facing, footprint_of(k, facing)) {}
};

class CargoHold : public Building {
public:
    CargoHold() : Building(ItemKind::CargoHold, Direction::North, footprint_of(ItemKind::CargoHold)) {}
};

class FluidPart : public Building {
public:
    FluidPart(ItemKind k, Direction facing) : Building(k, facing, footprint_of(k, facing)) {}
};

// Runs recipes of the categories it handles, at its crafting speed.
class Machine : public Building {
public:
    explicit Machine(ItemKind k) : Building(k, Direction::North, footprint_of(k)) {}

    bool crafts(CraftCategory category) const;
    bool can_run(const Recipe& recipe) const { return crafts(recipe.category); }
    // Recipe time multiplier: 2 runs a recipe in half its ticks.
    int crafting_speed() const;
};

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
