// test_logistics.cpp — headless tests for moving items between buildings:
// belts (travel time, spacing and jams, which sides they take items from),
// buildings feeding their neighbors directly, machines crafting the recipe
// they were set to, filter sorters, and a whole collector -> crusher -> belt
// -> bridge chain.
#include "sim/building.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/world.h"

#include <cassert>
#include <cstdio>

namespace {

// The bridge is at (c, c); the starting hull spans c +-2 on both axes.
constexpr int c = kShipGridSize / 2;

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

// Three belts heading west into the bridge carry an item there in exactly
// three cells' travel time.
void test_belt_to_bridge() {
    World world;
    Ship& ship = world.get_ship();
    assert(ship.place(ItemKind::Hull, {c + 3, c}) == BuildResult::Ok);
    for (int x = c + 1; x <= c + 3; ++x)
        assert(ship.place(ItemKind::TransportBelt, {x, c}, Direction::West) == BuildResult::Ok);

    // Not from the cell it points at; from behind, it starts at the back.
    assert(!ship.insert_item({c + 3, c}, {c + 2, c}, ItemKind::IronPlate));
    assert(ship.insert_item({c + 3, c}, {c + 4, c}, ItemKind::IronPlate));
    assert(belt_at(ship, {c + 3, c}).get_items().front().pos == 0);

    const int plates = ship.inventory().count(ItemKind::IronPlate);
    const Tick per_cell = belt_cell_ticks();
    run(world, 3 * per_cell - 1);
    assert(ship.inventory().count(ItemKind::IronPlate) == plates);
    assert(belt_at(ship, {c + 1, c}).get_items().size() == 1);
    run(world, 1);
    assert(ship.inventory().count(ItemKind::IronPlate) == plates + 1);
    for (int x = c + 1; x <= c + 3; ++x)
        assert(belt_at(ship, {x, c}).get_items().empty());
}

// A belt into nothing fills up, keeping its items apart, then refuses more;
// removing it refunds what was on it.
void test_belt_jam() {
    World world;
    Ship& ship = world.get_ship();
    const GridPos p{c + 1, c - 2};  // pointing north, off the hull
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
    const GridPos p{c + 1, c - 2};
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
        assert(ship.place(ItemKind::AsteroidCollector, {c + 1, c}) == BuildResult::Ok);
        const auto& collector = dynamic_cast<const Collector&>(*ship.building_at({c + 1, c}));
        const int before = ship.inventory().total();
        run(world, collector.get_spec().interval * 3);
        assert(ship.inventory().total() == before + 3);
        assert(collector.stored() == 0);
    }
    {
        World world;
        Ship& ship = world.get_ship();
        assert(ship.place(ItemKind::AsteroidCollector, {c + 1, c + 1}) == BuildResult::Ok);
        assert(ship.place(ItemKind::TransportBelt, {c, c + 1}, Direction::North) == BuildResult::Ok);
        const auto& collector = dynamic_cast<const Collector&>(*ship.building_at({c + 1, c + 1}));
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
    const GridPos p{c + 1, c + 1};  // diagonal from the bridge: no neighbors
    const GridPos east{c + 3, c + 1}, from_east{c + 2, c + 1};
    assert(ship.place(ItemKind::SolarFurnace, p) == BuildResult::Ok);
    const Machine& furnace = machine_at(ship, p);
    const Recipe& iron = *recipe_for(ItemKind::IronPlate);
    const Recipe& silicon = *recipe_for(ItemKind::Silicon);

    // No recipe: takes nothing. Can't run arc smelting.
    assert(!furnace.get_recipe());
    assert(!ship.insert_item(from_east, east, ItemKind::IronOre));
    assert(!ship.set_recipe(p, &silicon));
    assert(!ship.set_recipe({c + 2, c - 2}, &iron));  // nothing there
    assert(ship.set_recipe({c + 2, c + 2}, &iron));   // any cell of it
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
    const GridPos p{c + 1, c + 1};
    ship.inventory().add(ItemKind::FabricatorMk1, 1);
    for (int y = c + 1; y <= c + 3; ++y)
        for (int x = c + 1; x <= c + 3; ++x)
            ship.place(ItemKind::Hull, {x, y});  // some are already hull
    assert(ship.place(ItemKind::FabricatorMk1, p) == BuildResult::Ok);
    const Recipe& gear = *recipe_for(ItemKind::Gear);
    assert(ship.set_recipe(p, &gear));
    const Machine& fab = machine_at(ship, p);
    for (Tick t = 0; t < gear.ticks * 10; ++t) {
        ship.insert_item({c + 3, c + 1}, {c + 4, c + 1}, ItemKind::IronPlate);
        world.advance_tick();
    }
    assert(fab.get_output().count(ItemKind::Gear) == kOutputCrafts);
    assert(!fab.crafting());
    assert(fab.get_input().count(ItemKind::IronPlate) == 2 * kInputCrafts);
}

// A collector feeds a crusher (only the chunks its recipe takes), which
// puts ore on a belt into the bridge. The other chunks back up in the
// collector until it stalls.
void test_chain() {
    World world;
    Ship& ship = world.get_ship();
    for (int x = c - 1; x <= c + 2; ++x)
        assert(ship.place(ItemKind::Hull, {x, c - 3}) == BuildResult::Ok);
    const GridPos crusher{c - 1, c - 3};
    const GridPos collector_pos{c + 1, c - 3};
    assert(ship.place(ItemKind::AsteroidCrusher, crusher) == BuildResult::Ok);
    assert(ship.place(ItemKind::AsteroidCollector, collector_pos) == BuildResult::Ok);
    assert(ship.place(ItemKind::TransportBelt, {c, c - 1}, Direction::South) == BuildResult::Ok);
    const Recipe* metallic = nullptr;
    for (const Recipe* r : machine_recipes(ItemKind::AsteroidCrusher))
        if (r->inputs.front().kind == ItemKind::MetallicChunk)
            metallic = r;
    assert(metallic && machine_recipes(ItemKind::AsteroidCrusher).size() == kChunkKindCount);
    assert(ship.set_recipe(crusher, metallic));

    const int iron = ship.inventory().count(ItemKind::IronOre);
    const int nickel = ship.inventory().count(ItemKind::NickelOre);
    run(world, 120 * kTicksPerSecond);
    const auto& collector = dynamic_cast<const Collector&>(*ship.building_at(collector_pos));
    assert(collector.full());
    assert(collector.items().count(ItemKind::MetallicChunk) == 0);
    const int crushed = (ship.inventory().count(ItemKind::IronOre) - iron) / 3;
    assert(crushed > 0);
    assert(ship.inventory().count(ItemKind::IronOre) - iron == crushed * 3);
    assert(ship.inventory().count(ItemKind::NickelOre) - nickel == crushed);
}

void test_cargo_hold() {
    World world;
    Ship& ship = world.get_ship();
    const GridPos p{c + 1, c + 1};
    assert(ship.place(ItemKind::CargoHold, p) == BuildResult::Ok);
    int stored = 0;
    while (ship.insert_item({c + 2, c + 1}, {c + 3, c + 1}, ItemKind::Steel))
        ++stored;
    assert(stored == kCargoCapacity);
    const int steel = ship.inventory().count(ItemKind::Steel);
    assert(ship.remove_building(p) == BuildResult::Ok);
    assert(ship.inventory().count(ItemKind::Steel) == steel + kCargoCapacity);
}

// A sorter one cell north-east of the bridge, with its west side as the
// way in and a belt leading away from each of the other three.
struct SorterRig {
    static constexpr GridPos sorter{c + 1, c - 1};
    static constexpr GridPos west{c, c - 1};
    static constexpr GridPos east{c + 2, c - 1};
    static constexpr GridPos north{c + 1, c - 2};
    static constexpr GridPos south{c + 1, c};

    explicit SorterRig(Ship& ship) {
        ship.inventory().add(ItemKind::FilterSorter, 1);
        assert(ship.place(ItemKind::FilterSorter, sorter) == BuildResult::Ok);
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
    assert(!ship.set_sort_filter({c - 1, c - 1}, ItemKind::Gear));    // nothing
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
        for (int x = c + 3; x <= c + 4; ++x)
            assert(ship.place(ItemKind::Hull, {x, c - 1}) == BuildResult::Ok);
        for (int x = c + 1; x <= c + 3; ++x) {
            assert(ship.place(ItemKind::FilterSorter, {x, c - 1}) == BuildResult::Ok);
            assert(ship.set_sort_filter({x, c - 1}, ItemKind::IronPlate));
        }
        assert(ship.place(ItemKind::TransportBelt, {c + 4, c - 1}, Direction::East) == BuildResult::Ok);
        assert(!ship.insert_item({c + 1, c - 1}, {c, c - 1}, ItemKind::IronPlate));
        assert(ship.remove_building({c + 3, c - 1}) == BuildResult::Ok);
        assert(ship.place(ItemKind::TransportBelt, {c + 3, c - 1}, Direction::East) == BuildResult::Ok);
        assert(ship.insert_item({c + 1, c - 1}, {c, c - 1}, ItemKind::IronPlate));
        assert(belt_at(ship, {c + 3, c - 1}).get_items().size() == 1);
    }
    {
        World world;
        Ship& ship = world.get_ship();
        ship.inventory().add(ItemKind::FilterSorter, 4);
        for (int y = c - 2; y <= c - 1; ++y)
            for (int x = c + 1; x <= c + 2; ++x)
                assert(ship.place(ItemKind::FilterSorter, {x, y}) == BuildResult::Ok);
        assert(!ship.insert_item({c + 1, c - 1}, {c, c - 1}, ItemKind::Gear));
    }
}

// A collector's chunks ride a belt into a sorter beside the bridge: the
// metallic ones carry straight on into the bridge, the rest turn into a
// cargo hold.
void test_sorter_feeds() {
    World world;
    Ship& ship = world.get_ship();
    for (int y = c - 1; y <= c; ++y)
        for (int x = c + 3; x <= c + 4; ++x)
            assert(ship.place(ItemKind::Hull, {x, y}) == BuildResult::Ok);
    ship.inventory().add(ItemKind::FilterSorter, 1);
    const GridPos sorter{c + 1, c};
    const GridPos hold_pos{c + 1, c + 1};  // the sorter's south side
    assert(ship.place(ItemKind::AsteroidCollector, {c + 3, c - 1}) == BuildResult::Ok);
    assert(ship.place(ItemKind::TransportBelt, {c + 2, c}, Direction::West) == BuildResult::Ok);
    assert(ship.place(ItemKind::FilterSorter, sorter) == BuildResult::Ok);
    assert(ship.place(ItemKind::CargoHold, hold_pos) == BuildResult::Ok);
    assert(ship.set_sort_filter(sorter, ItemKind::MetallicChunk));

    const Inventory before = ship.inventory();
    run(world, 120 * kTicksPerSecond);
    const auto& hold = dynamic_cast<const CargoHold&>(*ship.building_at(hold_pos));
    assert(ship.inventory().count(ItemKind::MetallicChunk) > before.count(ItemKind::MetallicChunk));
    assert(hold.get_stored().count(ItemKind::MetallicChunk) == 0);
    assert(hold.get_stored().total() > 0);
    for (int k = 0; k < kChunkKindCount; ++k) {
        const ItemKind chunk = static_cast<ItemKind>(static_cast<int>(ItemKind::MetallicChunk) + k);
        if (chunk != ItemKind::MetallicChunk)
            assert(ship.inventory().count(chunk) == before.count(chunk));
    }
}

void test_machine_recipes() {
    for (const Recipe* r : machine_recipes(ItemKind::FabricatorMk1))
        assert(r->category == CraftCategory::Assembly);
    assert(machine_recipes(ItemKind::ArcFurnace).size() > machine_recipes(ItemKind::SolarFurnace).size());
    assert(machine_recipes(ItemKind::Pipe).empty());
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
    test_machine_recipes();
    std::puts("test_logistics: all passed");
    return 0;
}
