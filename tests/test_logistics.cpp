// test_logistics.cpp — headless tests for moving items between buildings:
// belts (travel time, spacing and jams, which sides they take items from),
// buildings feeding their neighbors directly, machines crafting the recipe
// they were set to, filter and reverse sorters, junctions, routers, and a
// whole collector -> furnace -> belt -> bridge chain.
#include "sim/building.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/world.h"

#include <cassert>
#include <cstdio>

namespace {

// The bridge covers b0..b1 on both axes, and the starting hull h0..h1; c,
// the middle of the grid, is a row (and column) through the bridge.
constexpr int c = kShipGridSize / 2;
constexpr int b0 = kStartBridgePos.x;
constexpr int b1 = b0 + kBridgeSize - 1;
constexpr int h0 = (kShipGridSize - kStartHullSide) / 2;
constexpr int h1 = h0 + kStartHullSide - 1;
static_assert(kStartBridgePos.x == kStartBridgePos.y && h0 <= b0 - 2 && h1 >= b1 + 2,
              "the tests build on two rows of starting hull around the bridge");

// Puts hull at `p`, unless the starting hull already covers it.
void ensure_hull(Ship& ship, GridPos p) {
    if (!ship.has_hull(p))
        assert(ship.place(ItemKind::Hull, p) == BuildResult::Ok);
}

// Ticks for an item to cross one transport belt cell.
Tick belt_cell_ticks() {
    return static_cast<Tick>(kBeltLength / belt_speed(ItemKind::TransportBelt));
}

void run(World& world, Tick ticks) {
    for (Tick t = 0; t < ticks; ++t)
        world.advance_tick();
}

const Conveyor& belt_at(const Ship& ship, GridPos p) {
    const auto* belt = dynamic_cast<const Conveyor*>(ship.building_at(p));
    assert(belt);
    return *belt;
}

const Machine& machine_at(const Ship& ship, GridPos p) {
    const auto* machine = dynamic_cast<const Machine*>(ship.building_at(p));
    assert(machine);
    return *machine;
}

// Three belts heading west into the bridge's east side carry an item there
// in exactly three cells' travel time.
void test_belt_to_bridge() {
    World world;
    Ship& ship = world.get_ship();
    for (int x = b1 + 1; x <= b1 + 3; ++x) {
        ensure_hull(ship, {x, c});
        assert(ship.place(ItemKind::TransportBelt, {x, c}, Direction::West) == BuildResult::Ok);
    }

    // Not from the cell it points at; from behind, it starts at the back.
    assert(!ship.insert_item({b1 + 3, c}, {b1 + 2, c}, ItemKind::IronPlate));
    assert(ship.insert_item({b1 + 3, c}, {b1 + 4, c}, ItemKind::IronPlate));
    assert(belt_at(ship, {b1 + 3, c}).get_items().front().pos == 0);

    const int plates = ship.inventory().count(ItemKind::IronPlate);
    const Tick per_cell = belt_cell_ticks();
    run(world, 3 * per_cell - 1);
    assert(ship.inventory().count(ItemKind::IronPlate) == plates);
    assert(belt_at(ship, {b1 + 1, c}).get_items().size() == 1);
    run(world, 1);
    assert(ship.inventory().count(ItemKind::IronPlate) == plates + 1);
    for (int x = b1 + 1; x <= b1 + 3; ++x)
        assert(belt_at(ship, {x, c}).get_items().empty());
}

// A belt into nothing fills up, keeping its items apart, then refuses more;
// removing it refunds what was on it.
void test_belt_jam() {
    World world;
    Ship& ship = world.get_ship();
    const GridPos p{c, b0 - 2};  // pointing north, into nothing
    assert(ship.place(ItemKind::TransportBelt, p, Direction::North) == BuildResult::Ok);
    const GridPos behind{p.x, p.y + 1};
    int accepted = 0;
    for (int t = 0; t < 300; ++t) {
        if (ship.insert_item(p, behind, ItemKind::Gear))
            ++accepted;
        world.advance_tick();
    }
    const int full = kBeltLength / kBeltSpacing + 1;
    assert(accepted == full);
    const std::vector<BeltItem>& items = belt_at(ship, p).get_items();
    assert(static_cast<int>(items.size()) == full);
    for (std::size_t i = 0; i < items.size(); ++i)
        assert(items[i].pos == kBeltLength - static_cast<int>(i) * kBeltSpacing);

    const int gears = ship.inventory().count(ItemKind::Gear);
    assert(ship.remove_building(p) == BuildResult::Ok);
    assert(ship.inventory().count(ItemKind::Gear) == gears + full);
}

// From the side, items join halfway along; but a belt nothing feeds from
// behind is a corner, and side input starts at its back.
void test_belt_sides() {
    World world;
    Ship& ship = world.get_ship();
    const GridPos p{c, b0 - 2};
    assert(ship.place(ItemKind::TransportBelt, p, Direction::East) == BuildResult::Ok);
    assert(ship.insert_item(p, {p.x, p.y - 1}, ItemKind::Gear));
    assert(belt_at(ship, p).get_items().front().pos == 0);

    assert(ship.place(ItemKind::TransportBelt, {p.x - 1, p.y}, Direction::East) == BuildResult::Ok);
    assert(ship.insert_item(p, {p.x, p.y + 1}, ItemKind::Glass));
    const std::vector<BeltItem>& items = belt_at(ship, p).get_items();
    assert(items.size() == 2);
    assert(items.front().kind == ItemKind::Glass && items.front().pos == kBeltLength / 2);
    // No room left at the middle.
    assert(!ship.insert_item(p, {p.x, p.y - 1}, ItemKind::Gear));
    // A belt facing into the side doesn't change that: it isn't behind.
    assert(ship.place(ItemKind::TransportBelt, {p.x + 1, p.y}, Direction::West) == BuildResult::Ok);
    assert(!ship.insert_item({p.x + 1, p.y}, p, ItemKind::Gear));  // p is its front cell
}

// Beside the bridge, a collector hands each catch straight in; one cell
// away, a belt between them carries it.
void test_collector_feeds() {
    {
        World world;
        Ship& ship = world.get_ship();
        assert(ship.place(ItemKind::AsteroidCollector, {b1 + 1, c}) == BuildResult::Ok);
        const auto& collector = dynamic_cast<const Collector&>(*ship.building_at({b1 + 1, c}));
        const int before = ship.inventory().total();
        run(world, collector.get_spec().interval * 3);
        assert(ship.inventory().total() == before + 3);
        assert(collector.stored() == 0);
    }
    {
        World world;
        Ship& ship = world.get_ship();
        assert(ship.place(ItemKind::AsteroidCollector, {b1 + 1, b1 + 1}) == BuildResult::Ok);
        assert(ship.place(ItemKind::TransportBelt, {b1, b1 + 1}, Direction::North) == BuildResult::Ok);
        const auto& collector = dynamic_cast<const Collector&>(*ship.building_at({b1 + 1, b1 + 1}));
        const int before = ship.inventory().total();
        const Tick per_cell = belt_cell_ticks();
        run(world, collector.get_spec().interval + per_cell - 1);
        assert(ship.inventory().total() == before);
        run(world, 1);
        assert(ship.inventory().total() == before + 1);
    }
}

void test_machine_recipe() {
    World world;
    Ship& ship = world.get_ship();
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
    Ship& ship = world.get_ship();
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

// A collector aimed at iron ore feeds a solar furnace, which puts plates on
// a belt into the bridge, keeping up with it. Aimed at something the
// furnace's recipe doesn't take, the collector backs up until it stalls.
void test_chain() {
    World world;
    Ship& ship = world.get_ship();
    // North of the bridge, a row apart from it: the belt between them
    // leads into it.
    for (int y = b0 - 3; y <= b0 - 2; ++y)
        for (int x = c - 1; x <= c + 2; ++x)
            ensure_hull(ship, {x, y});
    const GridPos furnace{c - 1, b0 - 3};
    const GridPos collector_pos{c + 1, b0 - 3};
    assert(ship.place(ItemKind::SolarFurnace, furnace) == BuildResult::Ok);
    assert(ship.place(ItemKind::AsteroidCollector, collector_pos) == BuildResult::Ok);
    assert(ship.place(ItemKind::TransportBelt, {c, b0 - 1}, Direction::South) == BuildResult::Ok);
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

void test_cargo_hold() {
    World world;
    Ship& ship = world.get_ship();
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

// A sorter (or another 1x1 building, `kind`) one cell diagonally off the
// bridge's north-east corner, with its west side as the way in and a belt
// leading away from each of the other three.
struct SorterRig {
    static constexpr GridPos sorter{b1 + 1, b0 - 1};
    static constexpr GridPos west{b1, b0 - 1};
    static constexpr GridPos east{b1 + 2, b0 - 1};
    static constexpr GridPos north{b1 + 1, b0 - 2};
    static constexpr GridPos south{b1 + 1, b0};

    explicit SorterRig(Ship& ship, ItemKind kind = ItemKind::FilterSorter) {
        ship.inventory().add(kind, 1);
        assert(ship.place(kind, sorter) == BuildResult::Ok);
        assert(ship.place(ItemKind::TransportBelt, east, Direction::East) == BuildResult::Ok);
        assert(ship.place(ItemKind::TransportBelt, north, Direction::North) == BuildResult::Ok);
        assert(ship.place(ItemKind::TransportBelt, south, Direction::South) == BuildResult::Ok);
    }
};

// What the filter matches carries straight on; the rest turns.
void test_sorter_straight() {
    World world;
    Ship& ship = world.get_ship();
    const SorterRig rig(ship);
    const Building& sorter = *ship.building_at(rig.sorter);
    assert(sorter.footprint() == (GridSize{1, 1}) && !sorter.rotatable());
    assert(ship.set_sort_filter(rig.sorter, ItemKind::IronPlate));

    assert(ship.insert_item(rig.sorter, rig.west, ItemKind::IronPlate));
    assert(belt_at(ship, rig.east).get_items().size() == 1);
    assert(belt_at(ship, rig.east).get_items().front().pos == 0);
    assert(ship.insert_item(rig.sorter, rig.west, ItemKind::Gear));
    assert(belt_at(ship, rig.east).get_items().size() == 1);
    assert(belt_at(ship, rig.north).get_items().size() + belt_at(ship, rig.south).get_items().size() == 1);
    // It holds nothing.
    assert(sorter.contents().empty());
}

// With no filter, everything turns, taking turns between the sides; a side
// that won't take an item leaves it all to the other, and with neither the
// item is refused.
void test_sorter_alternates() {
    World world;
    Ship& ship = world.get_ship();
    const SorterRig rig(ship);
    const Tick per_cell = belt_cell_ticks();
    for (int i = 0; i < 4; ++i) {
        assert(ship.insert_item(rig.sorter, rig.west, ItemKind::Gear));
        run(world, per_cell);
    }
    assert(belt_at(ship, rig.north).get_items().size() == 2);
    assert(belt_at(ship, rig.south).get_items().size() == 2);
    assert(belt_at(ship, rig.east).get_items().empty());

    // No north belt: all of it goes south, until that belt jams.
    assert(ship.remove_building(rig.north) == BuildResult::Ok);
    int accepted = 0;
    for (int t = 0; t < 300; ++t) {
        if (ship.insert_item(rig.sorter, rig.west, ItemKind::Gear))
            ++accepted;
        world.advance_tick();
    }
    const int full = kBeltLength / kBeltSpacing + 1;
    assert(accepted == full - 2);
    assert(static_cast<int>(belt_at(ship, rig.south).get_items().size()) == full);
    assert(!ship.insert_item(rig.sorter, rig.west, ItemKind::Gear));
}

void test_sorter_filter() {
    World world;
    Ship& ship = world.get_ship();
    const SorterRig rig(ship);
    const auto& sorter = dynamic_cast<const Sorter&>(*ship.building_at(rig.sorter));
    assert(!sorter.get_filter());
    assert(!ship.set_sort_filter(rig.east, ItemKind::Gear));          // a belt
    assert(!ship.set_sort_filter({b0 - 1, b0 - 1}, ItemKind::Gear));  // nothing
    assert(ship.set_sort_filter(rig.sorter, ItemKind::Gear));
    assert(sorter.get_filter() == ItemKind::Gear);
    assert(ship.set_sort_filter(rig.sorter, std::nullopt));
    assert(!sorter.get_filter());

    const int total = ship.inventory().total();
    const int sorters = ship.inventory().count(ItemKind::FilterSorter);
    assert(ship.remove_building(rig.sorter) == BuildResult::Ok);
    assert(ship.inventory().total() == total + 1);
    assert(ship.inventory().count(ItemKind::FilterSorter) == sorters + 1);
}

// Two sorters in a row pass an item on, but not into a third; a ring of
// them refuses it rather than passing it round forever.
void test_sorter_chain() {
    {
        World world;
        Ship& ship = world.get_ship();
        ship.inventory().add(ItemKind::FilterSorter, 3);
        const int y = b0 - 2;  // a row apart from the bridge
        for (int x = c + 1; x <= c + 3; ++x) {
            assert(ship.place(ItemKind::FilterSorter, {x, y}) == BuildResult::Ok);
            assert(ship.set_sort_filter({x, y}, ItemKind::IronPlate));
        }
        assert(ship.place(ItemKind::TransportBelt, {c + 4, y}, Direction::East) == BuildResult::Ok);
        assert(!ship.insert_item({c + 1, y}, {c, y}, ItemKind::IronPlate));
        assert(ship.remove_building({c + 3, y}) == BuildResult::Ok);
        assert(ship.place(ItemKind::TransportBelt, {c + 3, y}, Direction::East) == BuildResult::Ok);
        assert(ship.insert_item({c + 1, y}, {c, y}, ItemKind::IronPlate));
        assert(belt_at(ship, {c + 3, y}).get_items().size() == 1);
    }
    {
        World world;
        Ship& ship = world.get_ship();
        ship.inventory().add(ItemKind::FilterSorter, 4);
        // Diagonally off the bridge's bottom-right corner, touching nothing.
        for (int y = b1 + 1; y <= b1 + 2; ++y)
            for (int x = b1 + 1; x <= b1 + 2; ++x)
                assert(ship.place(ItemKind::FilterSorter, {x, y}) == BuildResult::Ok);
        assert(!ship.insert_item({b1 + 1, b1 + 1}, {b1, b1 + 1}, ItemKind::Gear));
    }
}

// A collector's catch rides a belt into a sorter beside the bridge: what
// the filter matches carries straight on into the bridge, the rest turns
// into a cargo hold.
void test_sorter_feeds() {
    World world;
    Ship& ship = world.get_ship();
    for (int y = c - 1; y <= c; ++y)
        for (int x = b1 + 3; x <= b1 + 4; ++x)
            ensure_hull(ship, {x, y});
    ship.inventory().add(ItemKind::FilterSorter, 1);
    const GridPos sorter{b1 + 1, c};
    const GridPos hold_pos{b1 + 1, c + 1};  // the sorter's south side
    assert(ship.place(ItemKind::AsteroidCollector, {b1 + 3, c - 1}) == BuildResult::Ok);
    assert(ship.place(ItemKind::TransportBelt, {b1 + 2, c}, Direction::West) == BuildResult::Ok);
    assert(ship.place(ItemKind::FilterSorter, sorter) == BuildResult::Ok);
    assert(ship.place(ItemKind::CargoHold, hold_pos) == BuildResult::Ok);
    assert(ship.set_sort_filter(sorter, ItemKind::IronOre));
    assert(ship.set_collector_target({b1 + 3, c - 1}, ItemKind::Silica));

    const Inventory before = ship.inventory();
    run(world, 60 * kTicksPerSecond);
    const auto& hold = dynamic_cast<const CargoHold&>(*ship.building_at(hold_pos));
    assert(ship.inventory().count(ItemKind::Silica) == before.count(ItemKind::Silica));
    assert(hold.get_stored().count(ItemKind::Silica) > 0);

    assert(ship.set_collector_target({b1 + 3, c - 1}, ItemKind::IronOre));
    run(world, 60 * kTicksPerSecond);
    assert(ship.inventory().count(ItemKind::IronOre) > before.count(ItemKind::IronOre));
    assert(hold.get_stored().count(ItemKind::IronOre) == 0);
}

// A sorter beside a furnace, just under the belt that feeds it: the plates
// it turns must not go onto that belt, which would carry them into the
// furnace (and jam there, as the furnace won't take plates).
void test_sorter_not_back_into_source() {
    World world;
    Ship& ship = world.get_ship();
    const GridPos furnace{b1 + 4, b1 + 2};
    for (int y = b1 + 1; y <= b1 + 4; ++y)
        for (int x = b1 + 1; x <= b1 + 5; ++x)
            ensure_hull(ship, {x, y});
    ship.inventory().add(ItemKind::FilterSorter, 1);
    const GridPos into{furnace.x - 1, furnace.y};    // points into the furnace
    const GridPos behind{furnace.x - 2, furnace.y};  // feeds it from behind
    assert(ship.place(ItemKind::SolarFurnace, furnace) == BuildResult::Ok);
    assert(ship.place(ItemKind::TransportBelt, into, Direction::East) == BuildResult::Ok);
    assert(ship.place(ItemKind::TransportBelt, behind, Direction::East) == BuildResult::Ok);
    assert(ship.place(ItemKind::FilterSorter, {furnace.x - 1, furnace.y + 1}) == BuildResult::Ok);
    assert(ship.set_recipe(furnace, recipe_for(ItemKind::IronPlate)));

    ship.inventory().add(ItemKind::IronOre, 100);
    for (Tick t = 0; t < 60 * kTicksPerSecond; ++t) {
        if (t % (3 * belt_cell_ticks()) == 0)
            ship.insert_item(behind, {behind.x - 1, behind.y}, ItemKind::IronOre);
        world.advance_tick();
        for (const BeltItem& item : belt_at(ship, into).get_items())
            assert(item.kind == ItemKind::IronOre);
    }
    assert(machine_at(ship, furnace).get_output().total() > 0);
}

// A reverse sorter turns what its filter matches and passes the rest
// straight on; with no filter, everything goes straight.
void test_reverse_sorter() {
    World world;
    Ship& ship = world.get_ship();
    const SorterRig rig(ship, ItemKind::ReverseSorter);
    const auto& sorter = dynamic_cast<const Sorter&>(*ship.building_at(rig.sorter));
    assert(sorter.inverted());

    assert(ship.insert_item(rig.sorter, rig.west, ItemKind::Gear));
    assert(belt_at(ship, rig.east).get_items().size() == 1);
    run(world, belt_cell_ticks());

    assert(ship.set_sort_filter(rig.sorter, ItemKind::IronPlate));
    for (int i = 0; i < 2; ++i) {
        assert(ship.insert_item(rig.sorter, rig.west, ItemKind::IronPlate));
        run(world, belt_cell_ticks());
    }
    assert(belt_at(ship, rig.north).get_items().size() == 1);
    assert(belt_at(ship, rig.south).get_items().size() == 1);
    assert(ship.insert_item(rig.sorter, rig.west, ItemKind::Gear));
    assert(belt_at(ship, rig.east).get_items().size() == 2);
    assert(belt_at(ship, rig.north).get_items().size() + belt_at(ship, rig.south).get_items().size() == 2);
}

// Items cross a junction straight, after kJunctionTicks, each way on its own:
// one way jamming doesn't hold up another. It takes nothing with nothing on
// the far side, and gives back what it holds when removed.
void test_junction() {
    World world;
    Ship& ship = world.get_ship();
    const SorterRig rig(ship, ItemKind::Junction);

    assert(ship.insert_item(rig.sorter, rig.west, ItemKind::IronPlate));
    run(world, kJunctionTicks - 1);
    assert(belt_at(ship, rig.east).get_items().empty());
    run(world, 1);
    assert(belt_at(ship, rig.east).get_items().size() == 1);
    assert(belt_at(ship, rig.east).get_items().front().pos == 0);

    // North to south: the south belt takes it from behind.
    assert(ship.insert_item(rig.sorter, rig.north, ItemKind::Gear));
    run(world, kJunctionTicks);
    assert(belt_at(ship, rig.south).get_items().size() == 1);
    // Nothing to the west to cross to.
    assert(!ship.insert_item(rig.sorter, rig.east, ItemKind::Gear));

    // Turn the east belt round, into the junction: west to east jams, with
    // a full queue, while north to south still flows.
    assert(ship.remove_building(rig.east) == BuildResult::Ok);
    assert(ship.place(ItemKind::TransportBelt, rig.east, Direction::West) == BuildResult::Ok);
    int queued = 0;
    while (ship.insert_item(rig.sorter, rig.west, ItemKind::IronPlate))
        ++queued;
    assert(queued == kJunctionCapacity);
    run(world, 2 * kJunctionTicks);
    assert(!ship.insert_item(rig.sorter, rig.west, ItemKind::IronPlate));
    assert(ship.insert_item(rig.sorter, rig.north, ItemKind::Gear));
    run(world, kJunctionTicks);
    assert(belt_at(ship, rig.south).get_items().size() == 2);

    const int plates = ship.inventory().count(ItemKind::IronPlate);
    assert(ship.remove_building(rig.sorter) == BuildResult::Ok);
    assert(ship.inventory().count(ItemKind::IronPlate) == plates + kJunctionCapacity);
}

// Two belt lines cross through a junction without mixing.
void test_junction_crossing() {
    World world;
    Ship& ship = world.get_ship();
    const int y = b0 - 4;  // the column down stops a row short of the bridge
    const GridPos junction{c, y};
    ship.inventory().add(ItemKind::Junction, 1);
    for (int x = c - 2; x <= c + 2; ++x)
        if (x != c) {
            ensure_hull(ship, {x, y});
            assert(ship.place(ItemKind::TransportBelt, {x, y}, Direction::East) == BuildResult::Ok);
        }
    for (int dy = -2; dy <= 2; ++dy)
        if (dy != 0) {
            ensure_hull(ship, {c, y + dy});
            assert(ship.place(ItemKind::TransportBelt, {c, y + dy}, Direction::South) == BuildResult::Ok);
        }
    ensure_hull(ship, junction);
    assert(ship.place(ItemKind::Junction, junction) == BuildResult::Ok);

    int fed = 0;
    for (Tick t = 0; t < 20 * kTicksPerSecond; ++t) {
        if (ship.insert_item({c - 2, y}, {c - 3, y}, ItemKind::IronPlate))
            ++fed;
        ship.insert_item({c, y - 2}, {c, y - 3}, ItemKind::Gear);
        world.advance_tick();
    }
    assert(fed > 20);  // it keeps up, rather than jamming at once
    for (const BeltItem& item : belt_at(ship, {c + 2, y}).get_items())
        assert(item.kind == ItemKind::IronPlate);
    for (const BeltItem& item : belt_at(ship, {c, y + 2}).get_items())
        assert(item.kind == ItemKind::Gear);
    assert(!belt_at(ship, {c + 2, y}).get_items().empty());
    assert(!belt_at(ship, {c, y + 2}).get_items().empty());
}

// A router holds one item at a time and, after kRouterTicks, hands it to its
// other three sides in turn, never back the way it came.
void test_router() {
    World world;
    Ship& ship = world.get_ship();
    const SorterRig rig(ship, ItemKind::Router);
    const auto& router = dynamic_cast<const Router&>(*ship.building_at(rig.sorter));

    assert(ship.insert_item(rig.sorter, rig.west, ItemKind::Gear));
    assert(!ship.insert_item(rig.sorter, rig.west, ItemKind::Gear));  // full
    assert(router.contents().size() == 1);
    run(world, kRouterTicks - 1);
    assert(router.get_held() == ItemKind::Gear);
    run(world, 1);
    assert(router.empty());
    for (int i = 0; i < 5; ++i) {
        assert(ship.insert_item(rig.sorter, rig.west, ItemKind::Gear));
        run(world, belt_cell_ticks());
    }
    // Six items over three sides: two each.
    assert(belt_at(ship, rig.north).get_items().size() == 2);
    assert(belt_at(ship, rig.east).get_items().size() == 2);
    assert(belt_at(ship, rig.south).get_items().size() == 2);

    // From the east, it may go west (onto a belt leading away that way), but
    // not back east.
    ship.inventory().add(ItemKind::TransportBelt, 1);
    assert(ship.place(ItemKind::TransportBelt, rig.west, Direction::West) == BuildResult::Ok);
    assert(ship.remove_building(rig.north) == BuildResult::Ok);
    assert(ship.remove_building(rig.south) == BuildResult::Ok);
    const std::size_t east = belt_at(ship, rig.east).get_items().size();
    for (int i = 0; i < 3; ++i) {
        assert(ship.insert_item(rig.sorter, rig.east, ItemKind::Steel));
        run(world, belt_cell_ticks());
    }
    assert(belt_at(ship, rig.west).get_items().size() == 3);
    assert(belt_at(ship, rig.east).get_items().size() == east);
}

void test_machine_recipes() {
    for (const Recipe* r : machine_recipes(ItemKind::FabricatorMk1))
        assert(r->category == CraftCategory::Assembly);
    assert(machine_recipes(ItemKind::Refinery).size() > machine_recipes(ItemKind::SolarFurnace).size());
    assert(machine_recipes(ItemKind::Junction).empty());
}

}  // namespace

int main() {
    test_belt_to_bridge();
    test_belt_jam();
    test_belt_sides();
    test_collector_feeds();
    test_machine_recipe();
    test_machine_output_full();
    test_chain();
    test_cargo_hold();
    test_sorter_straight();
    test_sorter_alternates();
    test_sorter_filter();
    test_sorter_chain();
    test_sorter_feeds();
    test_sorter_not_back_into_source();
    test_reverse_sorter();
    test_junction();
    test_junction_crossing();
    test_router();
    test_machine_recipes();
    std::puts("test_logistics: all passed");
    return 0;
}
