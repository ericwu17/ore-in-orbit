// building.h — everything that stands on the hull, as a class hierarchy,
// plus the bridge's Inventory and hand-crafting queue.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
//   Building (abstract)
//   ├── Bridge       the ship's command center; owns the inventory, follows
//   │                the fleet's milestone progress, hand-crafts
//   ├── Collector    gathers from space: asteroid collector, tractor array
//   ├── Conveyor     moves items along: belts (the belt tunnel doesn't work
//   │                yet)
//   ├── Sorter       routes items by kind (filter and reverse sorters)
//   ├── Junction     lets two belt lines cross
//   ├── Router       spreads items out to its neighbors
//   ├── CargoHold    item storage
//   ├── Machine      runs recipes: furnace, refinery, fabricators
//   ├── PowerPlant   solar panel / array, battery bank (nothing uses power
//   │                yet)
//   ├── Thruster     ion thruster (part of the ship's layout): charged with
//   │                fuel cells for a jump
//   ├── Turret       autocannon, railgun: fire the ammunition belts bring
//   └── ShipSystem   survey scanner
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
// Ship::tick).
#pragma once

#include "sim/game_types.h"
#include "sim/milestones.h"
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

// The footprint a building of this kind will have when facing `facing`.
// Rotatable buildings that aren't square turn with it: a
// 2x1 one facing north or south would be 1x2 facing east or west.
GridSize footprint_of(ItemKind kind, Direction facing = Direction::North);
// Whether the player picks a direction when placing it (belts).
bool is_rotatable(ItemKind kind);
// Buildings that can be unlocked and built but don't do anything yet: the
// belt tunnel (see belt_speed) and the power buildings (nothing uses power).
bool is_unfinished(ItemKind kind);
// The damage a building of this kind takes before it's destroyed (nullopt:
// the bridge).
int max_hp_of(std::optional<ItemKind> kind);

class Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    std::optional<ItemKind> placed_by;
    Direction dir;
    GridSize size;
    int hp;  // 0 to max_hp(): at 0, destroyed

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

    // Health: enemies wear it down (see combat.h), and it mends while no
    // enemy is near.
    int get_hp() const { return hp; }
    int max_hp() const { return max_hp_of(placed_by); }
    bool damaged() const { return hp < max_hp(); }
    // Takes `amount` of damage, down to 0; true if that destroyed it.
    bool take_damage(int amount);
    // Mends `amount`, up to max_hp().
    void repair(int amount);
    // The tick it was last hit, for the UI to flash it (not saved).
    Tick hit_at = 0;
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
    // The milestones are the fleet's: every ship's bridge shares one
    // Progress (see Fleet), and deliveries to any of them count. A bridge
    // on its own (a lone Ship) has one of its own.
    std::shared_ptr<Progress> progress = std::make_shared<Progress>();

public:
    Bridge();

    bool removable() const override { return false; }
    // Advances the front craft job; a finished one puts its outputs in the
    // inventory.
    void tick() override;
    // The ship's core: belts deliver anything into the inventory, and what
    // they deliver counts toward the current milestone.
    bool accepts(ItemKind) const override { return true; }
    void receive(ItemKind kind) override;

    Inventory& get_inventory() { return inventory; }
    const Inventory& get_inventory() const { return inventory; }
    Progress& get_progress() { return *progress; }
    const Progress& get_progress() const { return *progress; }
    // Follows `shared` from now on, instead of the progress it had.
    void share_progress(std::shared_ptr<Progress> shared) { progress = std::move(shared); }

    // Whether queue_craft(recipe) would succeed now.
    bool can_hand_craft(const Recipe& recipe) const;
    // Takes the recipe's inputs from the inventory and queues it; false (and
    // no change) if it isn't hand-craftable, isn't unlocked yet or the inputs
    // aren't there.
    bool queue_craft(const Recipe& recipe);
    // Drops queued job `index`, refunding its inputs; false if there is none.
    bool cancel_craft(std::size_t index);
    const std::deque<CraftJob>& craft_queue() const { return queue; }
};

// How a collector gathers: one catch every `interval` ticks into its
// buffer, which holds up to `capacity`; it stalls while the buffer is full.
// A catch is one of the raw resource it is aimed at, one of `targets`.
struct CollectorSpec {
    Tick interval = 0;
    int capacity = 0;  // items
    std::vector<ItemKind> targets;  // the first is what a new one aims at
};

// The spec of a collector kind (asteroid collector, tractor array).
const CollectorSpec& collector_spec(ItemKind kind);

class Collector : public Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    const CollectorSpec* spec;
    // Toward the next catch, in hundredths of a tick at a yield of 100%
    // (a catch at spec->interval * 100).
    Tick progress = 0;
    Inventory caught;   // raw resources
    ItemKind target;    // what it catches, one of spec->targets
    int yield = 100;    // of `target` where the ship is: % of the home sector's

public:
    explicit Collector(ItemKind k);

    const CollectorSpec& get_spec() const { return *spec; }
    ItemKind get_target() const { return target; }
    // Whether it can be aimed at `kind` (one of its spec's targets).
    bool can_target(ItemKind kind) const;
    // Aims it at `kind`, which can_target() must allow. What it already
    // caught stays in the buffer.
    void set_target(ItemKind kind);
    // Gathers at the yield last set, unless the buffer is full.
    void tick() override;
    // The yield of its target where the ship is (see Ship::set_yields): it
    // gathers that many % as fast as at home; 0 stalls it.
    int get_yield() const { return yield; }
    void set_yield(int percent) { yield = percent; }
    std::vector<ItemStack> contents() const override { return caught.stacks(); }
    Inventory* output_buffer() override { return &caught; }

    const Inventory& items() const { return caught; }
    Inventory& items() { return caught; }
    // How many items the buffer holds, out of capacity().
    int stored() const { return caught.total(); }
    int capacity() const { return spec->capacity; }
    bool full() const { return stored() >= capacity(); }
    // Items gathered per second while not full, at its yield.
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
// conveyors that don't move items yet (the tunnel).
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
//
// The reverse sorter (Mindustry's inverted sorter) is the same the other way
// round: matching items turn, the rest go straight; with no filter, every
// item goes straight.
class Sorter : public Building {
private:
    std::optional<ItemKind> filter;

public:
    // `k` is FilterSorter or ReverseSorter.
    explicit Sorter(ItemKind k) : Building(k, Direction::North, footprint_of(k)) {}

    // Whether it's a reverse sorter.
    bool inverted() const { return item_kind() == ItemKind::ReverseSorter; }
    // Whether `kind` goes straight through (else it turns).
    bool passes_straight(ItemKind kind) const { return (filter == kind) != inverted(); }
    std::optional<ItemKind> get_filter() const { return filter; }
    void set_filter(std::optional<ItemKind> item) { filter = item; }
    // Which side an item that turns tries first: right of its travel
    // direction, or left. Kept by the Ship, which flips it after each turn.
    bool next_right = false;
};

// A junction's queue for each way across holds up to kJunctionCapacity
// items, and each takes kJunctionTicks to cross.
constexpr int kJunctionCapacity = 6;
constexpr Tick kJunctionTicks = kTicksPerSecond / 3;

struct JunctionItem {
    ItemKind kind;
    Tick elapsed = 0;  // ticks since it came in, up to kJunctionTicks
};

// The junction, as in Mindustry: items cross it straight, out the side
// opposite the one they came in by, so two belt lines can cross without
// mixing. Each of the four ways across is its own queue: it takes an item
// coming in one way while that queue has room and something stands on the
// far side, and hands its front item on once it has crossed (see
// Ship::insert_item and Ship::tick). One way jamming doesn't hold up the
// others.
class Junction : public Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    // Indexed by the Direction the items travel; front first.
    std::array<std::deque<JunctionItem>, 4> lanes;

public:
    Junction() : Building(ItemKind::Junction, Direction::North, footprint_of(ItemKind::Junction)) {}

    // Moves every item on across.
    void tick() override;
    std::vector<ItemStack> contents() const override;

    // The items crossing toward `travel`, front first.
    const std::deque<JunctionItem>& lane(Direction travel) const {
        return lanes[static_cast<std::size_t>(travel)];
    }
    bool has_room(Direction travel) const { return lane(travel).size() < kJunctionCapacity; }
    // Adds an item crossing toward `travel`; has_room(travel) must hold.
    void insert(ItemKind kind, Direction travel);
    // The front item crossing toward `travel`, once it has crossed.
    std::optional<ItemKind> item_ready(Direction travel) const;
    void pop(Direction travel);
};

// Ticks a router holds an item before it can pass it on.
constexpr Tick kRouterTicks = 4;

// The router, as in Mindustry: it takes one item at a time, from any side,
// and after kRouterTicks hands it on to one of its other three neighbors,
// never back the way it came, taking turns among those that will take it
// (see Ship::insert_item and Ship::tick).
class Router : public Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    std::optional<ItemKind> held;
    Direction came_from = Direction::North;  // the side `held` came in by
    Tick elapsed = 0;                         // ticks `held` has been here, up to kRouterTicks

public:
    Router() : Building(ItemKind::Router, Direction::North, footprint_of(ItemKind::Router)) {}

    void tick() override;
    std::vector<ItemStack> contents() const override;

    std::optional<ItemKind> get_held() const { return held; }
    Direction get_came_from() const { return came_from; }
    bool empty() const { return !held; }
    // Takes an item that came in by side `from`; empty() must hold.
    void insert(ItemKind kind, Direction from);
    // The held item, once it may go on.
    std::optional<ItemKind> item_ready() const;
    void pop();
    // Which way the next item goes first (round robin, kept by the Ship).
    Direction next_out = Direction::North;
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

// A machine's buffers, in crafts of its recipe: it takes ingredients until
// it holds kInputCrafts crafts' worth, and stops crafting while a product
// has kOutputCrafts crafts' worth waiting to leave.
constexpr int kInputCrafts = 2;
constexpr int kOutputCrafts = 4;

// Runs the recipe the player picked (one of the categories it handles), at
// its crafting speed: ingredients come in from belts and neighbors, a craft
// starts once all of them are there and there's room for
// the products, which then wait in the output buffer to go out.
class Machine : public Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    const Recipe* recipe = nullptr;
    Inventory input;
    Inventory output;
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

// Fuel cells an ion thruster holds: full, it's charged for a jump.
constexpr int kThrusterCharge = 10;

// The ion thruster: takes fuel cells from belts until charged. A jump
// needs every thruster on the ship charged, and uses it all (see
// World::jump). Thrusters come with the ship's layout: they can't be
// removed, and one shot down to 0 isn't destroyed but knocked offline,
// taking no fuel and not counting for a jump until it mends.
class Thruster : public Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    int fuel = 0;  // fuel cells, up to kThrusterCharge

public:
    Thruster() : Building(ItemKind::IonThruster, Direction::North, footprint_of(ItemKind::IonThruster)) {}

    bool removable() const override { return false; }
    int get_fuel() const { return fuel; }
    bool charged() const { return fuel >= kThrusterCharge; }
    bool offline() const { return get_hp() == 0; }
    // Burns all its fuel (a jump).
    void burn() { fuel = 0; }
    std::vector<ItemStack> contents() const override;
    bool accepts(ItemKind kind) const override { return kind == ItemKind::FuelCell && !charged() && !offline(); }
    void receive(ItemKind) override { ++fuel; }
};

// What a turret fires and how: one item of `ammo` is `shots_per_ammo`
// shots, each doing `damage` to an enemy within `range` cells of its middle,
// one every `interval` ticks.
struct TurretSpec {
    ItemKind ammo;
    int shots_per_ammo = 1;
    float range = 0.0f;
    int damage = 0;
    Tick interval = 0;
};

// The spec of a turret kind (autocannon, railgun).
const TurretSpec& turret_spec(ItemKind kind);

// Items of ammunition a turret holds, waiting to be loaded.
constexpr int kTurretAmmoCapacity = 10;

// A turret: takes its ammunition from belts, and fires at the nearest enemy
// in range when it's ready (see combat.h). Out of ammunition, it's silent.
class Turret : public Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    const TurretSpec* spec;
    int ammo = 0;       // items waiting
    int shots = 0;      // left in the item loaded
    Tick cooldown = 0;  // ticks until it can fire again

public:
    explicit Turret(ItemKind k);

    const TurretSpec& get_spec() const { return *spec; }
    int get_ammo() const { return ammo; }
    int get_shots() const { return shots; }
    // Shots it can fire before it needs more ammunition.
    int shots_left() const { return shots + ammo * spec->shots_per_ammo; }
    bool ready() const { return cooldown == 0 && shots_left() > 0; }
    // Fires one shot (ready() must hold), loading an item if it must.
    void fire();
    void tick() override;
    std::vector<ItemStack> contents() const override;
    bool accepts(ItemKind kind) const override { return kind == spec->ammo && ammo < kTurretAmmoCapacity; }
    void receive(ItemKind) override { ++ammo; }

    // The last shot, for the UI (not saved): when, and where it went (in
    // cells, as Enemy positions are).
    Tick fired_at = 0;
    float aim_x = 0.0f;
    float aim_y = 0.0f;
};

// The survey scanner: surveys the sectors next to the ship's (see World).
class ShipSystem : public Building {
public:
    explicit ShipSystem(ItemKind k) : Building(k, Direction::North, footprint_of(k)) {}
};

// Which building kinds handle a category (the machines; hand-crafting at the
// bridge, which isn't listed, takes any recipe marked hand_craftable).
bool machine_crafts(ItemKind kind, CraftCategory category);
int machine_crafting_speed(ItemKind kind);

// A new building of the given kind (any is_building kind, the layout parts
// included). `facing` only matters for rotatable kinds.
std::unique_ptr<Building> make_building(ItemKind kind, Direction facing);
