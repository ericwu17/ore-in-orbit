// test_logistics.cpp — headless tests for moving items between buildings:
// tubes (travel time, spacing and jams, buildings first, forks taking turns,
// turning round where the tubes end, colors), check valves, filter and
// overflow tubes, crossovers, vacuum lines, extractors, buildings feeding
// their neighbors directly, machines crafting the recipe they were set to,
// and whole collector -> furnace -> tube -> bridge chains.
#include "sim/building.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"
#include "sim/world.h"

#include <cassert>
#include <cstdio>

namespace {

// The bridge covers b0..b1 on both axes (the starter layout has it on the
// diagonal); c, the middle of the grid, is a row (and column) through it.
constexpr int c = kShipGridSize / 2;
const int b0 = layout_to_grid(starter_layout(), starter_layout().bridge).x;
const int b1 = b0 + kBridgeSize - 1;

// The tests build on the starting hull, which must cover `p`.
void ensure_hull(const Ship& ship, GridPos p) {
    assert(ship.has_hull(p));
    (void)ship;
    (void)p;
}

// Ticks for an item to cross one tube cell.
constexpr Tick kTubeCellTicks = kTubeLength / kTubeSpeed;

void run(World& world, Tick ticks) {
    for (Tick t = 0; t < ticks; ++t)
        world.advance_tick();
}

// Places one `kind` at `p` (adding it to the inventory first), a tube piece
// in `color`.
void build(Ship& ship, ItemKind kind, GridPos p, Direction facing = Direction::North,
           TubeColor color = TubeColor::Amber) {
    ensure_hull(ship, p);
    ship.inventory().add(kind, 1);
    const BuildResult placed = ship.place(kind, p, facing);
    assert(placed == BuildResult::Ok);
    (void)placed;
    if (is_tube(kind)) {
        const bool colored = ship.set_tube_color(p, color);
        assert(colored);
        (void)colored;
    }
}

const Tube& tube_at(const Ship& ship, GridPos p) {
    const auto* tube = dynamic_cast<const Tube*>(ship.building_at(p));
    assert(tube);
    return *tube;
}

const VacuumLine& line_at(const Ship& ship, GridPos p) {
    const auto* line = dynamic_cast<const VacuumLine*>(ship.building_at(p));
    assert(line);
    return *line;
}

const CargoHold& hold_at(const Ship& ship, GridPos p) {
    const auto* hold = dynamic_cast<const CargoHold*>(ship.building_at(p));
    assert(hold);
    return *hold;
}

// Puts `n` of `kind` into the cargo hold covering `p`.
void stock_hold(Ship& ship, GridPos p, ItemKind kind, int n) {
    for (int i = 0; i < n; ++i) {
        const bool stocked = ship.insert_item(p, {p.x, p.y - 1}, kind);
        assert(stocked);
        (void)stocked;
    }
}

const Machine& machine_at(const Ship& ship, GridPos p) {
    const auto* machine = dynamic_cast<const Machine*>(ship.building_at(p));
    assert(machine);
    return *machine;
}

// Feeds `items` into the tube at `to` from the tube piece at `from`, one
// whenever it has room, then runs on for `after` ticks.
void feed(World& world, GridPos to, GridPos from, const std::vector<ItemKind>& items, Tick after) {
    Ship& ship = world.flagship();
    std::size_t next = 0;
    for (int t = 0; next < items.size(); ++t) {
        assert(t < 10'000);  // it never takes them all
        if (ship.insert_item(to, from, items[next]))
            ++next;
        world.advance_tick();
    }
    run(world, after);
}

// Three tubes from a check valve west into the bridge's east side carry an
// item there in exactly three cells' travel time.
void test_tube_to_bridge() {
    World world;
    Ship& ship = world.flagship();
    for (int x = b1 + 1; x <= b1 + 3; ++x)
        build(ship, ItemKind::Tube, {x, c});
    const GridPos valve{b1 + 4, c};
    build(ship, ItemKind::CheckValve, valve, Direction::West);
    assert(ship.tube_links({b1 + 1, c}, Direction::West));  // into the bridge
    assert(!ship.tube_links({b1 + 1, c}, Direction::North));  // nothing there

    // Not from an empty cell: tubes only take from what they link to.
    assert(!ship.insert_item({b1 + 3, c}, {b1 + 3, c - 1}, ItemKind::IronPlate));
    assert(ship.insert_item({b1 + 3, c}, valve, ItemKind::IronPlate));
    const TubeItem& item = tube_at(ship, {b1 + 3, c}).get_items().front();
    assert(item.pos == 0 && item.from == Direction::East && item.to == Direction::West);

    const int plates = ship.inventory().count(ItemKind::IronPlate);
    run(world, 3 * kTubeCellTicks - 1);
    assert(ship.inventory().count(ItemKind::IronPlate) == plates);
    assert(tube_at(ship, {b1 + 1, c}).get_items().size() == 1);
    run(world, 1);
    assert(ship.inventory().count(ItemKind::IronPlate) == plates + 1);
    for (int x = b1 + 1; x <= b1 + 3; ++x)
        assert(tube_at(ship, {x, c}).get_items().empty());
    // Tubes deliver like anything else: it counts toward the milestone.
    assert(ship.get_bridge().get_progress().delivered_count(ItemKind::IronPlate) == 1);
}

// A tube with nowhere to go, fed from a check valve (which won't take items
// back through its front), fills up, keeping its items apart, then refuses
// more and shows as jammed; removing it refunds what was in it.
void test_tube_jam() {
    World world;
    Ship& ship = world.flagship();
    const GridPos valve{c + 6, b0 - 3};
    const GridPos p{c + 7, b0 - 3};
    build(ship, ItemKind::CheckValve, valve, Direction::East);
    build(ship, ItemKind::Tube, p);
    int accepted = 0;
    for (int t = 0; t < 300; ++t) {
        if (ship.insert_item(p, valve, ItemKind::Gear))
            ++accepted;
        world.advance_tick();
    }
    const int full = kTubeLength / kTubeSpacing + 1;
    assert(accepted == full);
    const Tube& tube = tube_at(ship, p);
    assert(static_cast<int>(tube.get_items().size()) == full);
    for (std::size_t i = 0; i < tube.get_items().size(); ++i)
        assert(tube.get_items()[i].pos == kTubeLength - static_cast<int>(i) * kTubeSpacing);
    assert(tube.jammed());

    const int gears = ship.inventory().count(ItemKind::Gear);
    assert(ship.remove_building(p) == BuildResult::Ok);
    assert(ship.inventory().count(ItemKind::Gear) == gears + full);
}

// Where the tubes end, an item turns round: it goes back and forth between
// two tubes until a building next to them takes it.
void test_tube_turns_round() {
    World world;
    Ship& ship = world.flagship();
    const GridPos valve{c + 5, b0 - 3};
    const GridPos a{c + 6, b0 - 3};
    const GridPos b{c + 7, b0 - 3};
    build(ship, ItemKind::CheckValve, valve, Direction::East);
    build(ship, ItemKind::Tube, a);
    build(ship, ItemKind::Tube, b);
    assert(ship.insert_item(a, valve, ItemKind::Gear));
    run(world, kTubeCellTicks + kTubeCellTicks / 2);
    // In b, on its way back.
    const Tube& tube_b = tube_at(ship, b);
    assert(tube_b.get_items().size() == 1);
    assert(tube_b.get_items().front().from == Direction::West && tube_b.get_items().front().to == Direction::West);
    run(world, 10 * kTubeCellTicks);
    assert(tube_at(ship, a).get_items().size() + tube_b.get_items().size() == 1);

    // A cargo hold next to b takes it the next time it passes.
    build(ship, ItemKind::CargoHold, {b.x, b.y - 2});
    run(world, 3 * kTubeCellTicks);
    assert(hold_at(ship, {b.x, b.y - 2}).get_stored().count(ItemKind::Gear) == 1);
}

// A fork from a check valve: a tube up to a cargo hold, and one down to
// another, round the cell `fork`.
struct ForkRig {
    const GridPos valve{c + 5, b0 - 3};
    const GridPos fork{c + 6, b0 - 3};
    const GridPos up{c + 6, b0 - 4};
    const GridPos down{c + 6, b0 - 2};
    const GridPos hold_up{c + 6, b0 - 6};  // 2x2, touching `up` from above
    const GridPos hold_down{c + 6, b0 - 1};  // 2x2, touching `down` from below

    ForkRig(Ship& ship, ItemKind up_kind, ItemKind down_kind) {
        build(ship, ItemKind::CheckValve, valve, Direction::East);
        build(ship, ItemKind::Tube, fork);
        build(ship, up_kind, up);
        build(ship, down_kind, down);
        build(ship, ItemKind::CargoHold, hold_up);
        build(ship, ItemKind::CargoHold, hold_down);
    }
};

// A fork sends items each way in turn.
void test_fork_takes_turns() {
    World world;
    Ship& ship = world.flagship();
    const ForkRig rig(ship, ItemKind::Tube, ItemKind::Tube);
    feed(world, rig.fork, rig.valve, std::vector<ItemKind>(10, ItemKind::Gear), 5 * kTubeCellTicks);
    assert(hold_at(ship, rig.hold_up).get_stored().count(ItemKind::Gear) == 5);
    assert(hold_at(ship, rig.hold_down).get_stored().count(ItemKind::Gear) == 5);
}

// A filter tube takes only its item, and that goes into it first, as into a
// building; the rest go the other way. With no filter it takes nothing.
void test_filter_tube() {
    World world;
    Ship& ship = world.flagship();
    const ForkRig rig(ship, ItemKind::FilterTube, ItemKind::Tube);
    assert(!ship.insert_item(rig.up, rig.fork, ItemKind::IronOre));  // no filter yet
    assert(ship.set_filter(rig.up, ItemKind::IronOre));
    assert(!ship.set_filter(rig.fork, ItemKind::IronOre));  // a plain tube has none
    std::vector<ItemKind> items;
    for (int i = 0; i < 6; ++i)
        items.push_back(i % 2 == 0 ? ItemKind::IronOre : ItemKind::CopperOre);
    feed(world, rig.fork, rig.valve, items, 5 * kTubeCellTicks);
    const Inventory& up = hold_at(ship, rig.hold_up).get_stored();
    const Inventory& down = hold_at(ship, rig.hold_down).get_stored();
    assert(up.count(ItemKind::IronOre) == 3 && up.total() == 3);
    assert(down.count(ItemKind::CopperOre) == 3 && down.total() == 3);
}

// An overflow tube takes items only once every other way is blocked: here,
// once the cargo hold up the other way is full.
void test_overflow_tube() {
    World world;
    Ship& ship = world.flagship();
    const ForkRig rig(ship, ItemKind::Tube, ItemKind::OverflowTube);
    stock_hold(ship, rig.hold_up, ItemKind::Steel, kCargoCapacity - 3);
    feed(world, rig.fork, rig.valve, std::vector<ItemKind>(10, ItemKind::Gear), 20 * kTubeCellTicks);
    assert(hold_at(ship, rig.hold_up).get_stored().count(ItemKind::Gear) == 3);
    assert(hold_at(ship, rig.hold_down).get_stored().count(ItemKind::Gear) == 7);
}

// A building puts what it makes into an overflow tube only when nothing
// else next to it takes it.
void test_output_overflow_last() {
    World world;
    Ship& ship = world.flagship();
    const GridPos collector_at{c + 6, b0 - 7};  // 2x2
    const GridPos plain{c + 5, b0 - 7};
    const GridPos overflow{c + 8, b0 - 7};
    const GridPos hold{c + 9, b0 - 7};
    build(ship, ItemKind::AsteroidCollector, collector_at);
    build(ship, ItemKind::Tube, plain);  // leads nowhere: fills up
    build(ship, ItemKind::OverflowTube, overflow);
    build(ship, ItemKind::CargoHold, hold);
    const auto& collector = dynamic_cast<const Collector&>(*ship.building_at(collector_at));
    run(world, 8 * collector.get_spec().interval + 2 * kTubeCellTicks);
    const int full = kTubeLength / kTubeSpacing + 1;
    assert(static_cast<int>(tube_at(ship, plain).get_items().size()) == full);
    assert(hold_at(ship, hold).get_stored().total() == 8 - full);
}

// Tubes of different colors don't link, so two lines run side by side
// without mixing.
void test_tube_colors() {
    World world;
    Ship& ship = world.flagship();
    const int amber_y = b0 - 3;
    const int teal_y = b0 - 4;
    build(ship, ItemKind::CheckValve, {c + 5, amber_y}, Direction::East);
    build(ship, ItemKind::CheckValve, {c + 5, teal_y}, Direction::East, TubeColor::Teal);
    for (int x = c + 6; x <= c + 9; ++x) {
        build(ship, ItemKind::Tube, {x, amber_y});
        build(ship, ItemKind::Tube, {x, teal_y}, Direction::North, TubeColor::Teal);
    }
    const GridPos amber_hold{c + 10, amber_y};  // 2x2: rows amber_y and below
    const GridPos teal_hold{c + 10, teal_y - 1};  // 2x2: rows teal_y and above
    build(ship, ItemKind::CargoHold, amber_hold);
    build(ship, ItemKind::CargoHold, teal_hold);
    assert(!ship.tube_links({c + 6, amber_y}, Direction::North));
    assert(!ship.tube_links({c + 6, teal_y}, Direction::South));
    assert(ship.tube_links({c + 6, teal_y}, Direction::East));

    for (int i = 0; i < 5; ++i) {
        feed(world, {c + 6, amber_y}, {c + 5, amber_y}, {ItemKind::IronOre}, 0);
        feed(world, {c + 6, teal_y}, {c + 5, teal_y}, {ItemKind::CopperOre}, 0);
    }
    run(world, 10 * kTubeCellTicks);
    const Inventory& amber = hold_at(ship, amber_hold).get_stored();
    const Inventory& teal = hold_at(ship, teal_hold).get_stored();
    assert(amber.count(ItemKind::IronOre) == 5 && amber.total() == 5);
    assert(teal.count(ItemKind::CopperOre) == 5 && teal.total() == 5);
}

// A check valve links only by its front and back, takes items only from
// behind, and passes them only out of its front.
void test_check_valve() {
    World world;
    Ship& ship = world.flagship();
    const GridPos valve{c + 6, b0 - 3};
    const GridPos back{c + 5, b0 - 3};
    const GridPos front{c + 7, b0 - 3};
    const GridPos side{c + 6, b0 - 4};
    build(ship, ItemKind::Tube, back);
    build(ship, ItemKind::CheckValve, valve, Direction::East);
    build(ship, ItemKind::Tube, front);
    build(ship, ItemKind::Tube, side);
    assert(ship.tube_links(valve, Direction::West) && ship.tube_links(valve, Direction::East));
    assert(!ship.tube_links(valve, Direction::North) && !ship.tube_links(side, Direction::South));
    assert(ship.tube_links(front, Direction::West) && ship.tube_links(back, Direction::East));
    assert(!ship.insert_item(valve, front, ItemKind::Gear));
    assert(!ship.insert_item(valve, side, ItemKind::Gear));
    assert(ship.insert_item(valve, back, ItemKind::Gear));
    run(world, kTubeCellTicks);
    assert(tube_at(ship, valve).get_items().empty());
    assert(tube_at(ship, front).get_items().size() == 1);
}

// Two lines, of either color, cross at a crossover without mixing.
void test_crossover() {
    World world;
    Ship& ship = world.flagship();
    const GridPos cross{c + 7, b0 - 5};
    // Amber, west to east into a cargo hold.
    build(ship, ItemKind::CheckValve, {c + 4, cross.y}, Direction::East);
    for (const int x : {c + 5, c + 6, c + 8, c + 9})
        build(ship, ItemKind::Tube, {x, cross.y});
    const GridPos amber_hold{c + 10, cross.y};
    build(ship, ItemKind::CargoHold, amber_hold);
    // Teal, north to south into another.
    build(ship, ItemKind::CheckValve, {cross.x, cross.y - 3}, Direction::South, TubeColor::Teal);
    for (const int y : {cross.y - 2, cross.y - 1, cross.y + 1, cross.y + 2})
        build(ship, ItemKind::Tube, {cross.x, y}, Direction::North, TubeColor::Teal);
    const GridPos teal_hold{cross.x, cross.y + 3};
    build(ship, ItemKind::CargoHold, teal_hold);
    build(ship, ItemKind::Crossover, cross);

    for (int i = 0; i < 5; ++i) {
        feed(world, {c + 5, cross.y}, {c + 4, cross.y}, {ItemKind::IronOre}, 0);
        feed(world, {cross.x, cross.y - 2}, {cross.x, cross.y - 3}, {ItemKind::CopperOre}, 0);
    }
    run(world, 10 * kTubeCellTicks);
    const Inventory& amber = hold_at(ship, amber_hold).get_stored();
    const Inventory& teal = hold_at(ship, teal_hold).get_stored();
    assert(amber.count(ItemKind::IronOre) == 5 && amber.total() == 5);
    assert(teal.count(ItemKind::CopperOre) == 5 && teal.total() == 5);
}

// Beside the bridge, a collector hands each catch straight in; one cell
// away, a tube between them carries it.
void test_collector_feeds() {
    {
        World world;
        Ship& ship = world.flagship();
        assert(ship.place(ItemKind::AsteroidCollector, {b1 + 1, c}) == BuildResult::Ok);
        const auto& collector = dynamic_cast<const Collector&>(*ship.building_at({b1 + 1, c}));
        const int before = ship.inventory().total();
        run(world, collector.get_spec().interval * 3);
        assert(ship.inventory().total() == before + 3);
        assert(collector.stored() == 0);
    }
    {
        World world;
        Ship& ship = world.flagship();
        assert(ship.place(ItemKind::AsteroidCollector, {b1 + 1, b1 + 1}) == BuildResult::Ok);
        assert(ship.place(ItemKind::Tube, {b1, b1 + 1}) == BuildResult::Ok);
        const auto& collector = dynamic_cast<const Collector&>(*ship.building_at({b1 + 1, b1 + 1}));
        const int before = ship.inventory().total();
        run(world, collector.get_spec().interval + kTubeCellTicks - 1);
        assert(ship.inventory().total() == before);
        run(world, 1);
        assert(ship.inventory().total() == before + 1);
    }
}

void test_machine_recipe() {
    World world;
    Ship& ship = world.flagship();
    const GridPos p{b1 + 1, b1 + 1};  // diagonal from the bridge: no neighbors
    const GridPos east{b1 + 3, b1 + 1}, from_east{b1 + 2, b1 + 1};
    assert(ship.place(ItemKind::SolarFurnace, p) == BuildResult::Ok);
    const Machine& furnace = machine_at(ship, p);
    const Recipe& iron = *recipe_for(ItemKind::IronPlate);
    const Recipe& silicon = *recipe_for(ItemKind::Silicon);

    // No recipe: takes nothing. Can't run refining.
    assert(!furnace.get_recipe());
    assert(!ship.insert_item(from_east, east, ItemKind::IronOre));
    assert(!ship.set_recipe(p, &silicon));
    assert(!ship.set_recipe({b1 + 1, b0}, &iron));      // nothing there
    assert(ship.set_recipe({b1 + 2, b1 + 2}, &iron));  // any cell of it
    assert(furnace.get_recipe() == &iron);

    // Only its ingredients, kInputCrafts crafts' worth.
    assert(!ship.insert_item(from_east, east, ItemKind::CopperOre));
    for (int i = 0; i < kInputCrafts; ++i)
        assert(ship.insert_item(from_east, east, ItemKind::IronOre));
    assert(!ship.insert_item(from_east, east, ItemKind::IronOre));

    // One craft per recipe time (speed 1), into the output buffer.
    assert(furnace.crafts_per_minute() == 60.0 * kTicksPerSecond / static_cast<double>(iron.ticks));
    run(world, iron.ticks - 1);
    assert(furnace.crafting() && furnace.get_output().total() == 0);
    run(world, 1);
    assert(furnace.get_output().count(ItemKind::IronPlate) == 1);
    run(world, iron.ticks * 3);
    assert(furnace.get_output().count(ItemKind::IronPlate) == 2);
    assert(!furnace.crafting());

    // Switching recipe refunds ingredients, even those of a craft under way;
    // removing it refunds what it made.
    assert(ship.insert_item(from_east, east, ItemKind::IronOre));
    assert(ship.insert_item(from_east, east, ItemKind::IronOre));
    run(world, 10);
    assert(furnace.crafting() && furnace.get_input().count(ItemKind::IronOre) == 1);
    const int ore = ship.inventory().count(ItemKind::IronOre);
    assert(ship.set_recipe(p, nullptr));
    assert(ship.inventory().count(ItemKind::IronOre) == ore + 2);
    assert(!furnace.crafting());
    const int plates = ship.inventory().count(ItemKind::IronPlate);
    assert(ship.remove_building(p) == BuildResult::Ok);
    assert(ship.inventory().count(ItemKind::IronPlate) == plates + 2);
}

// Output waits while there's nowhere for it to go, and the machine stops
// once kOutputCrafts crafts' worth is waiting.
void test_machine_output_full() {
    World world;
    Ship& ship = world.flagship();
    const GridPos p{b1 + 1, b1 + 1};
    ship.inventory().add(ItemKind::FabricatorMk1, 1);
    for (int y = b1 + 1; y <= b1 + 3; ++y)
        for (int x = b1 + 1; x <= b1 + 3; ++x)
            ensure_hull(ship, {x, y});
    assert(ship.place(ItemKind::FabricatorMk1, p) == BuildResult::Ok);
    const Recipe& gear = *recipe_for(ItemKind::Gear);
    assert(ship.set_recipe(p, &gear));
    const Machine& fab = machine_at(ship, p);
    for (Tick t = 0; t < gear.ticks * 10; ++t) {
        ship.insert_item({b1 + 3, b1 + 1}, {b1 + 4, b1 + 1}, ItemKind::IronPlate);
        world.advance_tick();
    }
    assert(fab.get_output().count(ItemKind::Gear) == kOutputCrafts);
    assert(!fab.crafting());
    assert(fab.get_input().count(ItemKind::IronPlate) == 2 * kInputCrafts);
}

// A collector aimed at iron ore feeds a solar furnace, which puts plates
// into a tube into the bridge, keeping up with it. Aimed at something the
// furnace's recipe doesn't take, the collector backs up until it stalls.
void test_chain() {
    World world;
    Ship& ship = world.flagship();
    // North of the bridge, a row apart from it: the tube between them
    // leads into it.
    for (int y = b0 - 3; y <= b0 - 2; ++y)
        for (int x = c - 1; x <= c + 2; ++x)
            ensure_hull(ship, {x, y});
    const GridPos furnace{c - 1, b0 - 3};
    const GridPos collector_pos{c + 1, b0 - 3};
    assert(ship.place(ItemKind::SolarFurnace, furnace) == BuildResult::Ok);
    assert(ship.place(ItemKind::AsteroidCollector, collector_pos) == BuildResult::Ok);
    assert(ship.place(ItemKind::Tube, {c, b0 - 1}) == BuildResult::Ok);
    assert(ship.set_recipe(furnace, recipe_for(ItemKind::IronPlate)));

    const auto& collector = dynamic_cast<const Collector&>(*ship.building_at(collector_pos));
    const int plates = ship.inventory().count(ItemKind::IronPlate);
    run(world, 30 * kTicksPerSecond);
    // One catch every two seconds, each smelted and delivered within a few.
    const int delivered = ship.inventory().count(ItemKind::IronPlate) - plates;
    assert(delivered >= 12 && delivered <= 15);
    assert(collector.stored() == 0);

    assert(ship.set_collector_target(collector_pos, ItemKind::Silica));
    run(world, 60 * kTicksPerSecond);
    assert(collector.full());
    assert(collector.items().count(ItemKind::Silica) == collector.capacity());
}

// One tube carries ore past a furnace and plates back out of it: ore goes
// into the furnace first (buildings come first), the furnace puts its
// plates into the same tube, and they ride on to the bridge with any ore
// left over. Plates that head upstream turn round where the tube ends at
// the check valve, so nothing jams and nothing is lost.
void test_one_tube_smelter() {
    World world;
    Ship& ship = world.flagship();
    const GridPos hold{b1 + 9, c};  // 2x2
    const GridPos extractor{b1 + 8, c};
    const GridPos valve{b1 + 7, c};
    build(ship, ItemKind::CargoHold, hold);
    build(ship, ItemKind::Extractor, extractor, Direction::West);
    build(ship, ItemKind::CheckValve, valve, Direction::West);
    for (int x = b1 + 1; x <= b1 + 6; ++x)
        build(ship, ItemKind::Tube, {x, c});
    const GridPos furnace{b1 + 3, c + 1};  // 2x2, under the tube
    build(ship, ItemKind::SolarFurnace, furnace);
    assert(ship.set_recipe(furnace, recipe_for(ItemKind::IronPlate)));
    constexpr int kOre = 30;
    stock_hold(ship, hold, ItemKind::IronOre, kOre);

    const int ore = ship.inventory().count(ItemKind::IronOre);
    const int plates = ship.inventory().count(ItemKind::IronPlate);
    run(world, 90 * kTicksPerSecond);
    assert(hold_at(ship, hold).get_stored().total() == 0);
    const int ore_in = ship.inventory().count(ItemKind::IronOre) - ore;
    const int plates_in = ship.inventory().count(ItemKind::IronPlate) - plates;
    const Machine& m = machine_at(ship, furnace);
    assert(m.get_input().total() == 0 && m.get_output().total() == 0 && !m.crafting());
    // Every ore delivered or smelted. The furnace takes ore only while it
    // has room, so the rest rides past it to the bridge.
    assert(ore_in + plates_in == kOre);
    assert(plates_in > 0);
    for (int x = b1 + 1; x <= b1 + 6; ++x)
        assert(tube_at(ship, {x, c}).get_items().empty() && !tube_at(ship, {x, c}).jammed());
}

void test_cargo_hold() {
    World world;
    Ship& ship = world.flagship();
    const GridPos p{b1 + 1, b1 + 1};
    assert(ship.place(ItemKind::CargoHold, p) == BuildResult::Ok);
    int stored = 0;
    while (ship.insert_item({b1 + 2, b1 + 1}, {b1 + 3, b1 + 1}, ItemKind::Steel))
        ++stored;
    assert(stored == kCargoCapacity);
    const int steel = ship.inventory().count(ItemKind::Steel);
    assert(ship.remove_building(p) == BuildResult::Ok);
    assert(ship.inventory().count(ItemKind::Steel) == steel + kCargoCapacity);
}

void test_machine_recipes() {
    for (const Recipe* r : machine_recipes(ItemKind::FabricatorMk1))
        assert(r->category == CraftCategory::Assembly);
    assert(machine_recipes(ItemKind::Refinery).size() > machine_recipes(ItemKind::SolarFurnace).size());
    assert(machine_recipes(ItemKind::Crossover).empty());
}

// Whether the vacuum line at `a` has the one at `b` as its exit.
bool vacuum_linked(const Ship& ship, GridPos a, GridPos b) {
    return line_at(ship, a).get_exit() == b && line_at(ship, b).get_entrance() == a;
}

// A world with vacuum lines to build, and a row on bare hull north of the
// bridge (running from `kVacuumRow` east, with room for a few rows below it).
constexpr GridPos kVacuumRow{c - 8, c - 8};

World vacuum_world() {
    World world;
    world.flagship().inventory().add(ItemKind::VacuumLine, 20);
    return world;
}

// Items pass over a building, come out at the exit and go on, as fast as
// the vacuum line's speed takes them over the distance.
void test_vacuum_carries() {
    World world = vacuum_world();
    Ship& ship = world.flagship();
    const GridPos entrance{b1 + 6, c};
    const GridPos exit{b1 + 2, c};
    assert(ship.place(ItemKind::VacuumLine, entrance, Direction::West) == BuildResult::Ok);
    assert(ship.place(ItemKind::VacuumLine, exit, Direction::West) == BuildResult::Ok);
    assert(ship.place(ItemKind::CargoHold, {b1 + 3, c}) == BuildResult::Ok);
    assert(ship.place(ItemKind::Tube, {b1 + 1, c}) == BuildResult::Ok);
    assert(vacuum_linked(ship, entrance, exit));
    assert(!line_at(ship, entrance).is_exit() && line_at(ship, exit).is_exit());
    assert(line_at(ship, entrance).length() == 4 * kTubeLength);
    assert(line_at(ship, exit).length() == kTubeLength);
    assert(ship.tube_links({b1 + 1, c}, Direction::East));  // the exit's front

    assert(ship.insert_item(entrance, {b1 + 7, c}, ItemKind::IronPlate));
    const int plates = ship.inventory().count(ItemKind::IronPlate);
    const Tick vacuum_cell = static_cast<Tick>(kTubeLength / kVacuumSpeed);
    run(world, 4 * vacuum_cell - 1);
    assert(line_at(ship, entrance).get_items().size() == 1);
    assert(line_at(ship, exit).get_items().empty());
    run(world, 1);
    assert(line_at(ship, entrance).get_items().empty());
    assert(line_at(ship, exit).get_items().size() == 1);
    assert(line_at(ship, exit).get_items().front().pos == 0);
    run(world, vacuum_cell + kTubeCellTicks - 1);
    assert(ship.inventory().count(ItemKind::IronPlate) == plates);
    run(world, 1);
    assert(ship.inventory().count(ItemKind::IronPlate) == plates + 1);
    // The cargo hold it went over never saw it.
    assert(hold_at(ship, {b1 + 3, c}).get_stored().total() == 0);
}

// A vacuum line links to the nearest one ahead facing the same way, in
// range, whichever was placed first; others on the line don't matter.
void test_vacuum_links() {
    World world = vacuum_world();
    Ship& ship = world.flagship();
    const auto row = [](int dx, int dy) { return GridPos{kVacuumRow.x + dx, kVacuumRow.y + dy}; };

    // In range, placed exit first.
    assert(ship.place(ItemKind::VacuumLine, row(kVacuumRange, 0), Direction::East) == BuildResult::Ok);
    assert(!line_at(ship, row(kVacuumRange, 0)).linked());
    assert(ship.place(ItemKind::VacuumLine, row(0, 0), Direction::East) == BuildResult::Ok);
    assert(vacuum_linked(ship, row(0, 0), row(kVacuumRange, 0)));
    assert(line_at(ship, row(0, 0)).get_distance() == kVacuumRange);

    // Out of range.
    assert(ship.place(ItemKind::VacuumLine, row(0, 1), Direction::East) == BuildResult::Ok);
    assert(ship.place(ItemKind::VacuumLine, row(kVacuumRange + 1, 1), Direction::East) == BuildResult::Ok);
    assert(!line_at(ship, row(0, 1)).linked() && !line_at(ship, row(kVacuumRange + 1, 1)).linked());

    // Lines the other way, and across, pass through.
    assert(ship.place(ItemKind::VacuumLine, row(0, 2), Direction::East) == BuildResult::Ok);
    assert(ship.place(ItemKind::VacuumLine, row(1, 2), Direction::West) == BuildResult::Ok);
    assert(ship.place(ItemKind::VacuumLine, row(2, 2), Direction::North) == BuildResult::Ok);
    assert(ship.place(ItemKind::VacuumLine, row(3, 2), Direction::East) == BuildResult::Ok);
    assert(ship.place(ItemKind::VacuumLine, row(4, 2), Direction::West) == BuildResult::Ok);
    assert(vacuum_linked(ship, row(0, 2), row(3, 2)));
    assert(vacuum_linked(ship, row(4, 2), row(1, 2)));
    assert(!line_at(ship, row(2, 2)).linked());
}

// A row pairs off from the back, and pairs off anew when one goes; what a
// new one would link to is known beforehand.
void test_vacuum_row() {
    World world = vacuum_world();
    Ship& ship = world.flagship();
    const auto at = [](int dx) { return GridPos{kVacuumRow.x + dx, kVacuumRow.y}; };
    for (const int dx : {6, 2, 0, 4})
        assert(ship.place(ItemKind::VacuumLine, at(dx), Direction::East) == BuildResult::Ok);
    assert(vacuum_linked(ship, at(0), at(2)));
    assert(vacuum_linked(ship, at(4), at(6)));

    assert(ship.remove_building(at(0)) == BuildResult::Ok);
    assert(vacuum_linked(ship, at(2), at(4)));
    assert(!line_at(ship, at(6)).linked());

    // At the back of the row: an entrance, with the first one as its exit.
    VacuumPreview preview = ship.vacuum_preview(at(0), Direction::East);
    assert(preview.partner == at(2) && !preview.is_exit);
    // Just past an exit: an entrance too.
    preview = ship.vacuum_preview(at(5), Direction::East);
    assert(preview.partner == at(6) && !preview.is_exit);
    // Ahead of the lone one: its exit.
    preview = ship.vacuum_preview(at(8), Direction::East);
    assert(preview.partner == at(6) && preview.is_exit);
    assert(ship.place(ItemKind::VacuumLine, at(8), Direction::East) == BuildResult::Ok);
    assert(vacuum_linked(ship, at(6), at(8)));
    // Facing another way, it's on its own.
    assert(!ship.vacuum_preview(at(3), Direction::North).partner);
}

// With no exit a vacuum line fills up and holds on to its items: nothing
// goes on to the cell it points at. Removing it refunds them.
void test_vacuum_alone() {
    World world = vacuum_world();
    Ship& ship = world.flagship();
    const GridPos p = kVacuumRow;
    const GridPos ahead{p.x + 1, p.y};
    assert(ship.place(ItemKind::VacuumLine, p, Direction::East) == BuildResult::Ok);
    assert(ship.place(ItemKind::Tube, ahead) == BuildResult::Ok);
    int accepted = 0;
    for (int t = 0; t < 300; ++t) {
        if (ship.insert_item(p, {p.x - 1, p.y}, ItemKind::Gear))
            ++accepted;
        world.advance_tick();
    }
    assert(accepted == kTubeLength / kTubeSpacing + 1);
    assert(tube_at(ship, ahead).get_items().empty());

    const int gears = ship.inventory().count(ItemKind::Gear);
    assert(ship.remove_building(p) == BuildResult::Ok);
    assert(ship.inventory().count(ItemKind::Gear) == gears + accepted);
}

// Taking the exit away leaves the items on their way in the entrance, past
// where it now ends; once it has an exit again (a nearer one, here), they
// all come out.
void test_vacuum_relink() {
    World world = vacuum_world();
    Ship& ship = world.flagship();
    const GridPos entrance = kVacuumRow;
    const GridPos far{entrance.x + 4, entrance.y};
    const GridPos near{entrance.x + 2, entrance.y};
    assert(ship.place(ItemKind::VacuumLine, entrance, Direction::East) == BuildResult::Ok);
    assert(ship.place(ItemKind::VacuumLine, far, Direction::East) == BuildResult::Ok);
    int inserted = 0;
    for (int t = 0; t < 45; ++t) {
        if (ship.insert_item(entrance, {entrance.x - 1, entrance.y}, ItemKind::Gear))
            ++inserted;
        world.advance_tick();
    }
    assert(inserted > 2);
    assert(line_at(ship, entrance).get_items().front().pos > 2 * kTubeLength);

    assert(ship.remove_building(far) == BuildResult::Ok);
    assert(ship.inventory().count(ItemKind::Gear) == 0);  // none got that far
    run(world, 200);
    assert(static_cast<int>(line_at(ship, entrance).get_items().size()) == inserted);

    assert(ship.place(ItemKind::VacuumLine, near, Direction::East) == BuildResult::Ok);
    assert(ship.place(ItemKind::CargoHold, {near.x + 1, near.y}) == BuildResult::Ok);
    run(world, 200);
    assert(line_at(ship, entrance).get_items().empty() && line_at(ship, near).get_items().empty());
    assert(hold_at(ship, {near.x + 1, near.y}).get_stored().total() == inserted);
}

// Removing an entrance refunds the items on their way too.
void test_vacuum_remove_entrance() {
    World world = vacuum_world();
    Ship& ship = world.flagship();
    const GridPos entrance = kVacuumRow;
    const GridPos exit{entrance.x + 4, entrance.y};
    assert(ship.place(ItemKind::VacuumLine, entrance, Direction::East) == BuildResult::Ok);
    assert(ship.place(ItemKind::VacuumLine, exit, Direction::East) == BuildResult::Ok);
    assert(ship.insert_item(entrance, {entrance.x - 1, entrance.y}, ItemKind::Gear));
    run(world, 30);
    assert(ship.remove_building(entrance) == BuildResult::Ok);
    assert(ship.inventory().count(ItemKind::Gear) == 1);
    assert(!line_at(ship, exit).linked());
}

// An entrance takes items from any side but its front; an exit takes none
// (its entrance hands them over).
void test_vacuum_sides() {
    World world = vacuum_world();
    Ship& ship = world.flagship();
    const GridPos entrance = kVacuumRow;
    const GridPos exit{entrance.x + 3, entrance.y};
    assert(ship.place(ItemKind::VacuumLine, entrance, Direction::East) == BuildResult::Ok);
    assert(ship.place(ItemKind::VacuumLine, exit, Direction::East) == BuildResult::Ok);

    assert(!ship.insert_item(exit, {exit.x - 1, exit.y}, ItemKind::Gear));
    assert(!ship.insert_item(exit, {exit.x, exit.y - 1}, ItemKind::Gear));
    assert(!ship.insert_item(exit, {exit.x + 1, exit.y}, ItemKind::Gear));

    assert(!ship.insert_item(entrance, {entrance.x + 1, entrance.y}, ItemKind::Gear));  // its front
    assert(ship.insert_item(entrance, {entrance.x, entrance.y + 1}, ItemKind::Gear));   // a side
    assert(line_at(ship, entrance).get_items().front().pos == 0);
}

// An extractor takes an item every kExtractorTicks out of the cargo hold
// behind it, into the tube in front, which carries them into the bridge:
// there they count toward the milestone.
void test_extractor_from_hold() {
    World world;
    Ship& ship = world.flagship();
    const GridPos hold{b1 + 4, c};
    const GridPos extractor{b1 + 3, c};
    build(ship, ItemKind::CargoHold, hold);
    build(ship, ItemKind::Extractor, extractor, Direction::West);
    for (int x = b1 + 1; x <= b1 + 2; ++x)
        build(ship, ItemKind::Tube, {x, c});
    stock_hold(ship, hold, ItemKind::IronPlate, 20);
    assert(ship.extract_source(extractor) == ship.building_at(hold));
    assert(ship.tube_links({b1 + 2, c}, Direction::East));

    const int plates = ship.inventory().count(ItemKind::IronPlate);
    run(world, kTicksPerSecond);
    const int per_second = static_cast<int>(kTicksPerSecond / kExtractorTicks);
    assert(hold_at(ship, hold).get_stored().count(ItemKind::IronPlate) == 20 - per_second);
    run(world, 10 * kTicksPerSecond);
    assert(hold_at(ship, hold).get_stored().total() == 0);
    assert(ship.inventory().count(ItemKind::IronPlate) == plates + 20);
    assert(ship.get_bridge().get_progress().delivered_count(ItemKind::IronPlate) == 20);

    // It holds nothing: removing it refunds just itself.
    const Inventory before = ship.inventory();
    assert(ship.remove_building(extractor) == BuildResult::Ok);
    assert(ship.inventory().total() == before.total() + 1);
}

// With no filter, an extractor takes every kind in turn; with one, only
// that kind. Into a full cargo hold, it takes nothing.
void test_extractor_filter() {
    World world;
    Ship& ship = world.flagship();
    const GridPos from{b1 + 4, c};
    const GridPos extractor{b1 + 3, c};
    const GridPos to{b1 + 1, c};
    build(ship, ItemKind::CargoHold, from);
    build(ship, ItemKind::Extractor, extractor, Direction::West);
    build(ship, ItemKind::CargoHold, to);
    stock_hold(ship, from, ItemKind::Steel, 5);
    stock_hold(ship, from, ItemKind::Gear, 5);

    run(world, 2 * kExtractorTicks);
    const Inventory& moved = hold_at(ship, to).get_stored();
    assert(moved.count(ItemKind::Steel) == 1 && moved.count(ItemKind::Gear) == 1);

    assert(ship.set_filter(extractor, ItemKind::Gear));
    run(world, 10 * kExtractorTicks);
    assert(moved.count(ItemKind::Steel) == 1 && moved.count(ItemKind::Gear) == 5);
    const Inventory& left = hold_at(ship, from).get_stored();
    assert(left.count(ItemKind::Steel) == 4 && left.count(ItemKind::Gear) == 0);

    assert(ship.set_filter(extractor, std::nullopt));
    stock_hold(ship, to, ItemKind::Glass, kCargoCapacity - moved.total());
    run(world, 10 * kExtractorTicks);
    assert(left.count(ItemKind::Steel) == 4);
}

// From the bridge: ammunition out to a turret until it's full. A filter
// for something the turret doesn't take moves nothing; with none, the
// kinds it won't take are passed over.
void test_extractor_from_bridge() {
    World world;
    Ship& ship = world.flagship();
    ship.inventory().add({{ItemKind::Autocannon, 1}, {ItemKind::SlugMagazine, 20}});
    const GridPos extractor{b1 + 1, c};
    const GridPos turret_at{b1 + 2, c};
    build(ship, ItemKind::Extractor, extractor, Direction::East);
    assert(ship.place(ItemKind::Autocannon, turret_at) == BuildResult::Ok);
    assert(ship.extract_source(extractor) == &ship.get_bridge());
    const auto& turret = dynamic_cast<const Turret&>(*ship.building_at(turret_at));

    assert(ship.set_filter(extractor, ItemKind::IronPlate));
    run(world, kTicksPerSecond);
    assert(turret.get_ammo() == 0);

    assert(ship.set_filter(extractor, std::nullopt));
    run(world, 5 * kTicksPerSecond);
    assert(turret.get_ammo() == kTurretAmmoCapacity);
    assert(ship.inventory().count(ItemKind::SlugMagazine) == 20 - kTurretAmmoCapacity);
}

// Without storage behind it (nothing, or a tube), an extractor moves
// nothing, and a tube behind it doesn't link to it.
void test_extractor_no_source() {
    World world;
    Ship& ship = world.flagship();
    const GridPos extractor{b1 + 3, c};
    const GridPos behind{b1 + 2, c};
    const GridPos hold{b1 + 4, c};
    build(ship, ItemKind::Extractor, extractor, Direction::East);
    build(ship, ItemKind::CargoHold, hold);
    assert(!ship.extract_source(extractor));
    assert(!ship.extract_source(hold));  // not an extractor

    build(ship, ItemKind::Tube, behind);
    assert(!ship.extract_source(extractor));
    assert(!ship.tube_links(behind, Direction::East));
    run(world, kTicksPerSecond);
    assert(hold_at(ship, hold).get_stored().total() == 0);
}

}  // namespace

// A check valve never turns an item round: with nothing at its front, the
// item waits in it rather than going back the way it came.
void test_valve_dead_end() {
    World world;
    Ship& ship = world.flagship();
    const GridPos tube{c + 5, b0 - 3};
    const GridPos valve{c + 6, b0 - 3};
    build(ship, ItemKind::Tube, tube);
    build(ship, ItemKind::CheckValve, valve, Direction::East);
    assert(ship.insert_item(valve, tube, ItemKind::Gear));
    run(world, 4 * kTubeCellTicks);
    assert(tube_at(ship, tube).get_items().empty());
    assert(tube_at(ship, valve).get_items().size() == 1 && tube_at(ship, valve).jammed());
}

// A vacuum line's exit and a crossover with nothing past it are no way on:
// items in the tube beside them turn round where the tubes end instead of
// waiting on them for good.
void test_no_way_on() {
    for (const bool vacuum : {true, false}) {
        World world;
        Ship& ship = world.flagship();
        const GridPos t1{c + 6, b0 - 3};
        const GridPos t2{c + 7, b0 - 3};
        const GridPos valve{c + 6, b0 - 4};
        if (vacuum) {
            build(ship, ItemKind::VacuumLine, {c + 2, b0 - 3}, Direction::East);
            build(ship, ItemKind::VacuumLine, {c + 5, b0 - 3}, Direction::East);
            assert(line_at(ship, {c + 5, b0 - 3}).is_exit());
        } else {
            build(ship, ItemKind::Crossover, {c + 5, b0 - 3});
        }
        build(ship, ItemKind::Tube, t1);
        build(ship, ItemKind::Tube, t2);
        build(ship, ItemKind::CheckValve, valve, Direction::South);
        for (int i = 0; i < 3; ++i)
            feed(world, t1, valve, {ItemKind::Gear}, kTubeCellTicks);
        for (Tick t = 0; t < 20 * kTubeCellTicks; ++t) {
            run(world, 1);
            assert(!tube_at(ship, t1).jammed() && !tube_at(ship, t2).jammed());
        }
        assert(tube_at(ship, t1).get_items().size() + tube_at(ship, t2).get_items().size() == 3);
    }
}

int main() {
    test_tube_to_bridge();
    test_tube_jam();
    test_tube_turns_round();
    test_fork_takes_turns();
    test_filter_tube();
    test_overflow_tube();
    test_output_overflow_last();
    test_tube_colors();
    test_check_valve();
    test_valve_dead_end();
    test_no_way_on();
    test_crossover();
    test_collector_feeds();
    test_machine_recipe();
    test_machine_output_full();
    test_chain();
    test_one_tube_smelter();
    test_cargo_hold();
    test_vacuum_carries();
    test_vacuum_links();
    test_vacuum_row();
    test_vacuum_alone();
    test_vacuum_relink();
    test_vacuum_remove_entrance();
    test_vacuum_sides();
    test_extractor_from_hold();
    test_extractor_filter();
    test_extractor_from_bridge();
    test_extractor_no_source();
    test_machine_recipes();
    std::puts("test_logistics: all passed");
    return 0;
}
