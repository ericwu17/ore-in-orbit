// test_ship.cpp — headless tests for the ship build grid: the starting ship
// and its layout, placement rules, multi-cell and rotated footprints, the
// layout's fixed parts, the bridge inventory and removal refunds.
#include "sim/building.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"
#include "sim/world.h"

#include <cassert>
#include <cstdio>

namespace {

// A layout-coordinate cell of the starter layout, on the build grid.
GridPos on_grid(GridPos p) {
    return layout_to_grid(starter_layout(), p);
}

// The starter layout's bridge on the grid, its middle, and its last column
// (and row).
const GridPos kBridgePos = on_grid(starter_layout().bridge);
const GridPos kCenter{kBridgePos.x + kBridgeSize / 2, kBridgePos.y + kBridgeSize / 2};
const int kBridgeEnd = kBridgePos.x + kBridgeSize - 1;
// On the hull, next to the bridge.
const GridPos kOnHull{kBridgeEnd + 1, kCenter.y};
// The hull's last column on the bridge's rows, and the cell just past it.
const GridPos kHullEdge = on_grid({layout_bounds(starter_layout()).w - 1, kCenter.y - on_grid({0, 0}).y});
const GridPos kOffHull{kHullEdge.x + 1, kHullEdge.y};

// A new game's ship, with a few of every building to try placing.
Ship stocked_ship() {
    Ship ship = Ship::new_game();
    for (int i = 0; i < kItemKindCount; ++i)
        if (is_placeable(static_cast<ItemKind>(i)))
            ship.inventory().add(static_cast<ItemKind>(i), 10);
    return ship;
}

void test_new_game() {
    World world;
    const Ship& ship = world.flagship();
    const ShipLayout& layout = starter_layout();
    assert(ship.get_layout() == &layout);
    const Building* bridge = ship.building_at(kCenter);
    assert(bridge == &ship.get_bridge());
    assert(!bridge->item_kind());
    assert(bridge->footprint() == (GridSize{kBridgeSize, kBridgeSize}));
    assert(ship.building_anchored_at(kBridgePos) == bridge);
    assert(ship.building_at({kBridgeEnd, kBridgeEnd}) == bridge);
    assert(ship.building_at({kBridgeEnd + 1, kBridgeEnd}) == nullptr);
    // The hull is the layout's, all of it and only it.
    assert(ship.hull_count() == layout_hull_count(layout));
    assert(ship.hull_bounds().second == layout_bounds(layout));
    assert(ship.has_hull(kCenter));
    assert(ship.has_hull(kHullEdge));
    assert(!ship.has_hull(kOffHull));
    // Its engines, where the layout has them.
    assert(ship.thrusters().size() == layout.engines.size());
    for (const GridPos e : layout.engines) {
        const Building* engine = ship.building_anchored_at(on_grid(e));
        assert(dynamic_cast<const Thruster*>(engine) != nullptr);
        assert(engine->item_kind() == ItemKind::IonThruster);
    }
    // Enough for a first line (collectors into furnaces, belts back to the
    // bridge), and only hand-craftable buildings: the rest is built.
    for (const ItemKind kind : {ItemKind::TransportBelt, ItemKind::AsteroidCollector, ItemKind::SolarFurnace,
                                ItemKind::FabricatorMk1})
        assert(ship.inventory().count(kind) > 0);
    for (const ItemStack& s : ship.inventory().stacks())
        if (const Recipe* r = recipe_for(s.kind); r && is_placeable(s.kind))
            assert(r->hand_craftable);
    for (int i = 0; i < 10; ++i)
        world.advance_tick();
    assert(world.get_tick() == 10);
}

void test_every_layout() {
    for (const ShipLayout& layout : all_layouts()) {
        const Ship ship = Ship::from_layout(layout);
        assert(ship.get_layout() == &layout);
        assert(ship.hull_count() == layout_hull_count(layout));
        assert(ship.thrusters().size() == layout.engines.size());
        assert(ship.inventory().total() == 0);
        assert(ship.building_anchored_at(layout_to_grid(layout, layout.bridge)) == &ship.get_bridge());
    }
}

void test_needs_hull() {
    Ship ship = stocked_ship();
    const int junctions = ship.inventory().count(ItemKind::Junction);
    assert(ship.place(ItemKind::Junction, kOffHull) == BuildResult::NoHull);
    assert(ship.inventory().count(ItemKind::Junction) == junctions);
    assert(ship.place(ItemKind::Junction, kHullEdge) == BuildResult::Ok);
    assert(ship.building_at(kHullEdge)->item_kind() == ItemKind::Junction);
    assert(ship.inventory().count(ItemKind::Junction) == junctions - 1);
}

void test_rejections() {
    Ship ship = stocked_ship();
    assert(ship.place(ItemKind::TransportBelt, kCenter) == BuildResult::Occupied);
    assert(ship.place(ItemKind::TransportBelt, kOnHull) == BuildResult::Ok);
    assert(ship.place(ItemKind::Junction, kOnHull) == BuildResult::Occupied);
    assert(ship.place(ItemKind::Junction, {-1, 0}) == BuildResult::OutOfBounds);
    assert(ship.place(ItemKind::Junction, {0, kShipGridSize}) == BuildResult::OutOfBounds);
    assert(ship.remove_building({kShipGridSize, 0}) == BuildResult::OutOfBounds);
}

void test_empty_inventory() {
    Ship ship = stocked_ship();
    while (ship.inventory().take(ItemKind::TransportBelt)) {
    }
    assert(ship.can_place(ItemKind::TransportBelt, kOnHull) == BuildResult::NoneInInventory);
    assert(ship.place(ItemKind::TransportBelt, kOnHull) == BuildResult::NoneInInventory);
    assert(ship.building_at(kOnHull) == nullptr);
    assert(ship.inventory().count(ItemKind::TransportBelt) == 0);
}

void test_remove_refunds() {
    Ship ship = stocked_ship();
    const int collectors = ship.inventory().count(ItemKind::AsteroidCollector);

    assert(ship.place(ItemKind::AsteroidCollector, kOnHull) == BuildResult::Ok);
    assert(ship.remove_building(kOnHull) == BuildResult::Ok);
    assert(ship.building_at(kOnHull) == nullptr);
    assert(ship.has_hull(kOnHull));
    assert(ship.inventory().count(ItemKind::AsteroidCollector) == collectors);
    assert(ship.remove_building(kOnHull) == BuildResult::NothingThere);
}

void test_layout_parts_stay() {
    Ship ship = stocked_ship();
    // The bridge and the engines can't come off.
    assert(ship.remove_building(kCenter) == BuildResult::NotRemovable);
    assert(ship.building_at(kCenter) == &ship.get_bridge());
    const GridPos engine = on_grid(starter_layout().engines.front());
    assert(ship.remove_building(engine) == BuildResult::NotRemovable);
    assert(dynamic_cast<const Thruster*>(ship.building_at(engine)) != nullptr);
    assert(ship.inventory().count(ItemKind::IonThruster) == 0);
    // Nor are engines items to place.
    assert(!is_placeable(ItemKind::IonThruster));
    assert(is_building(ItemKind::IonThruster));
    assert(recipe_for(ItemKind::IonThruster) == nullptr);
}

void test_engine_offline() {
    Thruster thruster;
    assert(!thruster.offline());
    assert(thruster.accepts(ItemKind::FuelCell));
    assert(thruster.take_damage(thruster.max_hp()));
    assert(thruster.offline());
    assert(!thruster.accepts(ItemKind::FuelCell));
    thruster.repair(1);
    assert(!thruster.offline());
    assert(thruster.accepts(ItemKind::FuelCell));
}

void test_belt_direction() {
    Ship ship = stocked_ship();
    assert(ship.place(ItemKind::TransportBelt, kOnHull, Direction::West) == BuildResult::Ok);
    const Building* belt = ship.building_at(kOnHull);
    assert(belt->rotatable());
    assert(belt->facing() == Direction::West);

    Direction d = Direction::North;
    assert((d = rotated_cw(d)) == Direction::East);
    assert((d = rotated_cw(d)) == Direction::South);
    assert((d = rotated_cw(d)) == Direction::West);
    assert(rotated_cw(d) == Direction::North);
}

void test_footprints() {
    Ship ship = stocked_ship();
    assert(footprint_of(ItemKind::AsteroidCollector) == (GridSize{2, 2}));
    assert(footprint_of(ItemKind::Junction) == (GridSize{1, 1}));

    // On the hull's last column, a collector would stick out past it.
    assert(ship.place(ItemKind::AsteroidCollector, kHullEdge) == BuildResult::NoHull);
    // Overlapping the bridge, even by one cell.
    assert(ship.place(ItemKind::AsteroidCollector, {kBridgeEnd, kBridgeEnd}) == BuildResult::Occupied);
    // Hanging off the grid's last row / column.
    assert(ship.can_place(ItemKind::AsteroidCollector, {kShipGridSize - 1, 0}) == BuildResult::OutOfBounds);

    const int collectors = ship.inventory().count(ItemKind::AsteroidCollector);
    assert(ship.place(ItemKind::AsteroidCollector, kOnHull) == BuildResult::Ok);
    const Building* collector = ship.building_at(kOnHull);
    assert(collector->footprint() == (GridSize{2, 2}));
    // Stored once, at its top-left cell.
    assert(ship.building_anchored_at(kOnHull) == collector);
    assert(ship.building_anchored_at({kOnHull.x + 1, kOnHull.y + 1}) == nullptr);
    assert(ship.anchor_of({kOnHull.x + 1, kOnHull.y + 1}) == kOnHull);
    // Every covered cell reports the same building; the cells around don't.
    assert(ship.building_at({kOnHull.x + 1, kOnHull.y}) == collector);
    assert(ship.building_at({kOnHull.x, kOnHull.y + 1}) == collector);
    assert(ship.building_at({kOnHull.x + 1, kOnHull.y + 1}) == collector);
    assert(ship.building_at({kOnHull.x, kOnHull.y - 1}) == nullptr);
    assert(ship.place(ItemKind::Junction, {kOnHull.x + 1, kOnHull.y + 1}) == BuildResult::Occupied);

    // Removing from any covered cell removes the whole thing, once.
    assert(ship.remove_building({kOnHull.x + 1, kOnHull.y + 1}) == BuildResult::Ok);
    assert(ship.building_anchored_at(kOnHull) == nullptr);
    assert(ship.inventory().count(ItemKind::AsteroidCollector) == collectors);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            assert(ship.building_at({kOnHull.x + x, kOnHull.y + y}) == nullptr);
            assert(ship.has_hull({kOnHull.x + x, kOnHull.y + y}));
        }
    assert(ship.place(ItemKind::Junction, {kOnHull.x + 1, kOnHull.y + 1}) == BuildResult::Ok);
}

void test_hierarchy() {
    for (const ItemKind kind : {ItemKind::AsteroidCollector, ItemKind::TractorArray}) {
        const auto b = make_building(kind, Direction::North);
        assert(dynamic_cast<const Collector*>(b.get()) != nullptr);
        assert(b->item_kind() == kind);
        assert(b->footprint() == footprint_of(kind));
    }
    const auto junction = make_building(ItemKind::Junction, Direction::East);
    assert(dynamic_cast<const Collector*>(junction.get()) == nullptr);
    assert(!junction->rotatable());
}

void test_rotated_footprint() {
    Ship ship = stocked_ship();
    // Every placeable building is square or rotatable, and every rotatable
    // one is square, so turning one leaves its footprint alone.
    for (int i = 0; i < kItemKindCount; ++i) {
        const ItemKind kind = static_cast<ItemKind>(i);
        if (!is_placeable(kind))
            continue;
        assert(footprint_of(kind).w == footprint_of(kind).h);
        if (is_rotatable(kind))
            assert(footprint_of(kind, Direction::East) == footprint_of(kind, Direction::North));
    }
    // Not rotatable: facing doesn't matter.
    assert(footprint_of(ItemKind::IonThruster, Direction::East) == (GridSize{2, 3}));

    assert(ship.place(ItemKind::BeltTunnel, kOnHull, Direction::East) == BuildResult::Ok);
    assert(ship.building_at({kOnHull.x, kOnHull.y + 1}) == nullptr);
    assert(ship.building_at(kOnHull)->footprint() == (GridSize{1, 1}));
}

}  // namespace

int main() {
    test_new_game();
    test_every_layout();
    test_needs_hull();
    test_rejections();
    test_empty_inventory();
    test_remove_refunds();
    test_layout_parts_stay();
    test_engine_offline();
    test_belt_direction();
    test_footprints();
    test_hierarchy();
    test_rotated_footprint();
    std::puts("test_ship: all passed");
    return 0;
}
