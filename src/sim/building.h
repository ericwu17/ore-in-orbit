// building.h — everything that stands on the hull, as a class hierarchy,
// plus the bridge's Inventory and hand-crafting queue.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
//   Building (abstract)
//   ├── Bridge       the ship's command center; owns the inventory and the
//   │                item requests, follows the fleet's milestone progress,
//   │                hand-crafts
//   ├── Collector    gathers from space: asteroid collector, tractor array
//   ├── Tube         moves items along: tubes, check valves, filter and
//   │                overflow tubes
//   ├── VacuumLine   two of them carry items fast over whatever is between
//   ├── Crossover    lets two tube lines cross
//   ├── Extractor    takes items out of the bridge or a cargo hold
//   ├── CargoHold    item storage
//   ├── Machine      runs recipes: furnace, refinery, fabricators
//   ├── Thruster     ion thruster (part of the ship's layout): charged with
//   │                fuel cells for a jump
//   ├── Turret       autocannon, railgun: fire the ammunition tubes bring
//   └── ShipSystem   survey scanner
//
// One class per family, not per building: each building's footprint and
// whether it rotates is data (footprint_of, is_rotatable), looked up by item
// kind before one exists. A building doesn't know where it is: the Ship
// stores it at its footprint's top-left cell.
//
// There are no inserters: tubes feed straight into the buildings next to
// them, and buildings put their output straight into the tubes (or into the
// buildings) next to them (see Tube for the rules). A building takes items in
// through accepts() / receive() and offers what it makes from its
// output_buffer(); the Ship moves them between neighbors each tick (see
// Ship::tick).
//
// The bridge's item requests say how many of an item the player wants kept
// in its inventory. A collector or machine set to run only while the bridge
// requests its product (product_of) is on standby while that request is met,
// or there is none; the Ship sets it each tick.
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
// Rotatable buildings that aren't square turn with it: a 2x1 one facing north
// or south would be 1x2 facing east or west.
GridSize footprint_of(ItemKind kind, Direction facing = Direction::North);
// Whether the player picks a direction when placing it (check valves,
// extractors, vacuum lines).
bool is_rotatable(ItemKind kind);
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
    // taking it (only after accepts() said yes). Tubes, vacuum lines and
    // crossovers are the exception: where an item comes from matters to
    // them (see Ship::insert_item).
    virtual bool accepts(ItemKind) const { return false; }
    virtual void receive(ItemKind) {}
    // Item output: what it has made, waiting to go to its neighbors (any
    // kind in it, one item per tick); null if it makes nothing.
    virtual Inventory* output_buffer() { return nullptr; }
    // Storage an extractor can take items out of (the bridge, cargo holds);
    // null if it isn't storage. unload() takes one `kind` out of it, which
    // it must hold.
    virtual const Inventory* storage() const { return nullptr; }
    virtual void unload(ItemKind) {}
    // Which neighbor gets offered output next (round robin, kept by the Ship).
    std::size_t next_output_side = 0;
};

// A recipe the bridge is hand-crafting. Its inputs left the inventory when
// it was queued.
struct CraftJob {
    const Recipe* recipe = nullptr;
    Tick elapsed = 0;
};

// The most of one item the bridge can be asked to keep.
constexpr int kMaxRequest = 1'000'000;

// The bridge's footprint is kBridgeSize square.
constexpr int kBridgeSize = 6;

class Bridge : public Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    Inventory inventory;
    std::deque<CraftJob> queue;  // front is in progress
    // How many of each item the player wants kept in the inventory, in the
    // order they were asked for; at most one per kind (a count may be 0).
    std::vector<ItemStack> requests;
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
    // The ship's core: tubes deliver anything into the inventory, and what
    // they deliver counts toward the current milestone.
    bool accepts(ItemKind) const override { return true; }
    void receive(ItemKind kind) override;
    // What an extractor takes counts against the current milestone.
    const Inventory* storage() const override { return &inventory; }
    void unload(ItemKind kind) override;

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

    // Item requests (see the top of this file).
    const std::vector<ItemStack>& get_requests() const { return requests; }
    // The count asked for of `kind`, if any.
    std::optional<int> request_for(ItemKind kind) const;
    // Asks for `count` (0 to kMaxRequest) of `kind`, or changes the count already
    // asked for.
    void set_request(ItemKind kind, int count);
    void remove_request(ItemKind kind);
    // Whether `kind` is asked for and the inventory holds fewer.
    bool request_unmet(ItemKind kind) const;
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
    bool only_when_requested = false;
    bool standby = false;  // kept by the Ship (not saved)

public:
    explicit Collector(ItemKind k);

    const CollectorSpec& get_spec() const { return *spec; }
    ItemKind get_target() const { return target; }
    // Whether it can be aimed at `kind` (one of its spec's targets).
    bool can_target(ItemKind kind) const;
    // Aims it at `kind`, which can_target() must allow. What it already
    // caught stays in the buffer.
    void set_target(ItemKind kind);
    // Whether it runs only while the bridge requests its target (see the
    // top of this file); new ones always run.
    bool get_only_when_requested() const { return only_when_requested; }
    void set_only_when_requested(bool on) { only_when_requested = on; }
    // On standby, it gathers nothing (its buffer still empties).
    bool on_standby() const { return standby; }
    void set_standby(bool on) { standby = on; }
    // Gathers at the yield last set, unless the buffer is full or it's on
    // standby.
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

// Pneumatic tubes. Items travel through tubes cell by cell, kTubeLength
// positions across each one, keeping at least kTubeSpacing apart, at
// kTubeSpeed positions a tick (2 cells a second, 6 items a second).
constexpr int kTubeLength = 60;
constexpr int kTubeSpacing = 20;
constexpr int kTubeSpeed = 2;
// How long a tube's front item waits with nowhere to go before the tube
// counts as jammed (outlined for the player).
constexpr Tick kTubeJamTicks = kTicksPerSecond;

// The tube pieces: the plain tube, the check valve, the filter tube and the
// overflow tube (one class, Tube). The crossover, the extractor and the
// vacuum line work with tubes but are not tubes themselves.
constexpr bool is_tube(ItemKind kind) {
    return kind == ItemKind::Tube || kind == ItemKind::CheckValve || kind == ItemKind::FilterTube ||
           kind == ItemKind::OverflowTube;
}

struct TubeItem {
    ItemKind kind;
    int pos = 0;      // 0 (where it came in) to kTubeLength (where it leaves)
    Direction from;   // the side of the cell it came in by
    Direction to;     // the side it is headed for, picked when it came in
                      // (`from` where the tubes end); it may leave by
                      // another if that one is blocked
};

// One cell of tube. The rules every tube piece follows (Ship::tick moves
// items between cells):
//
//  1. A tube links to every neighboring tube piece of its own color and to
//     every other building next to it. Nothing faces a way.
//  2. Items keep moving and never go back the way they came. Where a tube
//     forks, items take the other ways in turn (round robin); an item with
//     nowhere open to go waits, and the tube backs up behind it. Only where
//     the tubes end (no more tube on, but the way it came, and no building
//     that takes the item now) does an item turn round and go back.
//  3. Buildings come first: an item passing a building that will take it
//     now goes into it, before trying another tube. Buildings put what they
//     make into any tube next to them.
//
// The other pieces each bend one rule:
//  - a check valve (CheckValve) takes items only from the cell behind it and
//    passes them only out of its front;
//  - a filter tube (FilterTube) takes only the item picked for it, and an
//    item that matches goes into it first, as into a building (rule 3); with
//    nothing picked it takes nothing;
//  - an overflow tube (OverflowTube) takes items only when every other way
//    on is blocked.
class Tube : public Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    std::vector<TubeItem> items;  // front (highest pos) first
    TubeColor color = TubeColor::Amber;
    std::optional<ItemKind> filter;  // a filter tube's

public:
    // `k` is one of is_tube's; `facing` only matters to a check valve.
    Tube(ItemKind k, Direction facing) : Building(k, facing, footprint_of(k, facing)) {}

    bool is_valve() const { return item_kind() == ItemKind::CheckValve; }
    bool is_filter() const { return item_kind() == ItemKind::FilterTube; }
    bool is_overflow() const { return item_kind() == ItemKind::OverflowTube; }

    TubeColor get_color() const { return color; }
    void set_color(TubeColor c) { color = c; }
    std::optional<ItemKind> get_filter() const { return filter; }
    void set_filter(std::optional<ItemKind> item) { filter = item; }
    // Whether it lets `kind` in at all: a filter tube only its filter's.
    bool admits(ItemKind kind) const { return !is_filter() || filter == kind; }

    // Moves every item forward, up to the far side or the item ahead.
    void tick() override;
    std::vector<ItemStack> contents() const override;

    const std::vector<TubeItem>& get_items() const { return items; }
    // Whether an item can come in now (kTubeSpacing clear behind the last).
    bool has_room() const { return items.empty() || items.back().pos >= kTubeSpacing; }
    // Takes an item in by side `from`, headed for side `to`; has_room() must
    // hold.
    void insert(ItemKind kind, Direction from, Direction to);
    // The front item, once it has reached the far side.
    const TubeItem* item_at_front() const;
    void pop_front();
    // Which side the next item to come in heads for first (round robin,
    // kept by the Ship).
    int next_out = 0;
    // How long the front item has waited at the far side (kept by the Ship;
    // not saved).
    Tick waited = 0;
    bool jammed() const { return waited >= kTubeJamTicks; }
};

// The ends of a vacuum line stand up to kVacuumRange cells apart, so it
// jumps over up to kVacuumRange - 1 cells of anything.
constexpr int kVacuumRange = 7;
// Positions a tick through a vacuum line: twice as fast as a tube, so it
// never holds up the tubes feeding it.
constexpr int kVacuumSpeed = 4;

struct VacuumItem {
    ItemKind kind;
    int pos = 0;
};

// The vacuum line: one end of a fast link that passes over whatever stands
// between its ends. It faces a way, and the Ship links each one to the
// nearest one ahead facing the same way, in range (see Ship::link_vacuum
// _lines), from where they stand alone: the one behind is the entrance, the
// one ahead the exit, whichever was placed first.
//
// An entrance takes items from any side but its front, and they travel on
// to its exit, so the items on their way belong to it, and its length()
// reaches the exit's far edge. Without an exit it holds them. An exit takes
// items only from its entrance, and passes them out of its front, into a
// tube or a building.
class VacuumLine : public Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    std::vector<VacuumItem> items;  // front (highest pos) first
    std::optional<GridPos> exit_cell;      // an entrance's exit, once linked
    std::optional<GridPos> entrance_cell;  // an exit's entrance
    int distance = 0;                      // cells to the other end, once linked

public:
    explicit VacuumLine(Direction facing)
        : Building(ItemKind::VacuumLine, facing, footprint_of(ItemKind::VacuumLine, facing)) {}

    // Where its items stop: its own far edge, or, for a linked entrance, its
    // exit's back edge. An item past it (its exit went, or a nearer one
    // came) stays where it is until it can go on.
    int length() const { return exit_cell ? kTubeLength * distance : kTubeLength; }
    bool is_exit() const { return entrance_cell.has_value(); }
    bool linked() const { return exit_cell || entrance_cell; }
    std::optional<GridPos> get_exit() const { return exit_cell; }
    std::optional<GridPos> get_entrance() const { return entrance_cell; }
    // Cells to the other end (0 when not linked).
    int get_distance() const { return distance; }

    // Moves every item forward, up to length() or the item ahead.
    void tick() override;
    std::vector<ItemStack> contents() const override;

    const std::vector<VacuumItem>& get_items() const { return items; }
    // Whether an item can come in at position 0.
    bool has_room() const { return items.empty() || items.back().pos >= kTubeSpacing; }
    // Takes an item in at position 0; has_room() must hold.
    void insert(ItemKind kind);
    // The front item, once it has reached length().
    std::optional<ItemKind> item_at_front() const;
    void pop_front();

    // Kept by the Ship.
    void unlink();
    void link_exit(GridPos exit, int cells);
    void link_entrance(GridPos entrance, int cells);
};

// A crossover's queue for each way across holds up to kCrossoverCapacity
// items, and each takes kCrossoverTicks to cross.
constexpr int kCrossoverCapacity = 6;
constexpr Tick kCrossoverTicks = kTicksPerSecond / 3;

struct CrossoverItem {
    ItemKind kind;
    Tick elapsed = 0;  // ticks since it came in, up to kCrossoverTicks
};

// The crossover: items cross it straight, out the side opposite the one
// they came in by, so two tube lines (of either color) can cross without
// mixing. Each of the four ways across is its own queue: it takes an item
// coming in one way while that queue has room and something stands on the
// far side, and hands its front item on once it has crossed (see
// Ship::insert_item and Ship::tick). One way jamming doesn't hold up the
// others.
class Crossover : public Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    // Indexed by the Direction the items travel; front first.
    std::array<std::deque<CrossoverItem>, 4> lanes;

public:
    Crossover() : Building(ItemKind::Crossover, Direction::North, footprint_of(ItemKind::Crossover)) {}

    // Moves every item on across.
    void tick() override;
    std::vector<ItemStack> contents() const override;

    // The items crossing toward `travel`, front first.
    const std::deque<CrossoverItem>& lane(Direction travel) const {
        return lanes[static_cast<std::size_t>(travel)];
    }
    bool has_room(Direction travel) const { return lane(travel).size() < kCrossoverCapacity; }
    // Adds an item crossing toward `travel`; has_room(travel) must hold.
    void insert(ItemKind kind, Direction travel);
    // The front item crossing toward `travel`, once it has crossed.
    std::optional<ItemKind> item_ready(Direction travel) const;
    void pop(Direction travel);
};

// Ticks an extractor waits after taking an item before it takes the next:
// 6 a second, all a tube carries.
constexpr Tick kExtractorTicks = 10;

// The extractor, the way items leave storage: it faces a way, and takes
// items out of the bridge or cargo hold behind it into whatever is in front
// (a tube, a machine, a turret, another hold), one every kExtractorTicks
// (see Ship::extract). It takes only the kind its filter picks, or with no
// filter every kind the storage holds, in turn. It holds nothing itself: it
// takes an item only if the cell in front takes it at once, and tubes can't
// feed it.
class Extractor : public Building {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    std::optional<ItemKind> filter;
    Tick cooldown = 0;  // ticks until it can take an item

public:
    explicit Extractor(Direction facing)
        : Building(ItemKind::Extractor, facing, footprint_of(ItemKind::Extractor, facing)) {}

    std::optional<ItemKind> get_filter() const { return filter; }
    void set_filter(std::optional<ItemKind> item) { filter = item; }
    void tick() override;
    bool ready() const { return cooldown == 0; }
    // It took one `kind`: wait, and with no filter try the kinds after it
    // first next time.
    void extracted(ItemKind kind);
    // The kind an unfiltered one tries first (round robin, kept with
    // extracted()).
    std::size_t next_kind = 0;
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
    const Inventory* storage() const override { return &stored; }
    void unload(ItemKind kind) override { stored.take(kind); }
};

// A machine's buffers, in crafts of its recipe: it takes ingredients until
// it holds kInputCrafts crafts' worth, and stops crafting while a product
// has kOutputCrafts crafts' worth waiting to leave.
constexpr int kInputCrafts = 2;
constexpr int kOutputCrafts = 4;

// Runs the recipe the player picked (one of the categories it handles), at
// its crafting speed: ingredients come in from tubes and neighbors, a craft
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
    bool only_when_requested = false;
    bool standby = false;  // kept by the Ship (not saved)

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

    // Whether it runs only while the bridge requests its recipe's product
    // (see the top of this file); new ones always run.
    bool get_only_when_requested() const { return only_when_requested; }
    void set_only_when_requested(bool on) { only_when_requested = on; }
    // On standby, it finishes the craft under way but starts no other (it
    // still takes ingredients, and its products still go out).
    bool on_standby() const { return standby; }
    void set_standby(bool on) { standby = on; }

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

// What a production building makes: a collector's target, or the main
// output of a machine's recipe; nullopt for anything else (or a machine with
// no recipe).
std::optional<ItemKind> product_of(const Building& b);

// The recipes a machine of this kind can run, in all_recipes() order.
std::vector<const Recipe*> machine_recipes(ItemKind kind);

// Fuel cells an ion thruster holds: full, it's charged for a jump.
constexpr int kThrusterCharge = 10;

// The ion thruster: takes fuel cells from tubes until charged. A jump
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

// A turret: takes its ammunition from tubes, and fires at the nearest enemy
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
