// test_ship.cpp — headless tests for the ship build grid: the starting ship,
// placement rules, multi-cell and rotated footprints, reinforced hull, the
// bridge inventory and removal refunds.
#include "sim/building.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/world.h"

#include <cassert>
#include <cstdio>

namespace {

// The middle of the grid, under the bridge.
constexpr GridPos kCenter{kShipGridSize / 2, kShipGridSize / 2};
// The bridge's last column (and row), and the starting hull's.
constexpr int kBridgeEnd = kStartBridgePos.x + kBridgeSize - 1;
constexpr int kHullStart = (kShipGridSize - kStartHullSide) / 2;
constexpr int kHullEnd = kHullStart + kStartHullSide - 1;
// Just outside the starting hull patch.
constexpr GridPos kOffHull{kHullEnd + 1, kCenter.y};
// On the starting hull, next to the bridge.
constexpr GridPos kOnHull{kBridgeEnd + 1, kCenter.y};

// A new game's ship, with a few of every building and plenty of hull to
// try placing.
Ship stocked_ship() {
    Ship ship = Ship::new_game();
    for (int i = 0; i < kItemKindCount; ++i)
        if (is_placeable(static_cast<ItemKind>(i)))
            ship.inventory().add(static_cast<ItemKind>(i), 10);
    ship.inventory().add(ItemKind::Hull, 100);
    return ship;
}

void test_new_game() {
    World world;
    const Ship& ship = world.get_ship();
    const Building* bridge = ship.building_at(kCenter);
    assert(bridge == &ship.get_bridge());
    assert(!bridge->item_kind());
    assert(bridge->footprint() == (GridSize{kBridgeSize, kBridgeSize}));
    assert(ship.building_anchored_at(kStartBridgePos) == bridge);
    assert(ship.building_at({kBridgeEnd, kBridgeEnd}) == bridge);
    assert(ship.building_at({kBridgeEnd + 1, kBridgeEnd}) == nullptr);
    assert(ship.has_hull(kCenter));
    assert(ship.has_hull({kHullStart, kHullStart}));
    assert(ship.has_hull({kHullEnd, kHullEnd}));
    assert(!ship.has_hull({kHullStart - 1, kHullStart}));
    assert(!ship.has_hull(kOffHull));
    // Enough for a first line (collectors into furnaces, belts back to the
    // bridge), and only hand-craftable buildings: the rest is built.
    for (const ItemKind kind : {ItemKind::Hull, ItemKind::TransportBelt, ItemKind::AsteroidCollector,
                                ItemKind::SolarFurnace, ItemKind::FabricatorMk1})
        assert(ship.inventory().count(kind) > 0);
    for (const ItemStack& s : ship.inventory().stacks())
        if (const Recipe* r = recipe_for(s.kind); r && is_placeable(s.kind))
            assert(r->hand_craftable);
    for (int i = 0; i < 10; ++i)
        world.advance_tick();
    assert(world.get_tick() == 10);
}

void test_needs_hull() {
    Ship ship = stocked_ship();
    const int junctions = ship.inventory().count(ItemKind::Junction);
    const int hull = ship.inventory().count(ItemKind::Hull);

    assert(ship.place(ItemKind::Junction, kOffHull) == BuildResult::NoHull);
    assert(ship.inventory().count(ItemKind::Junction) == junctions);

    assert(ship.place(ItemKind::Hull, kOffHull) == BuildResult::Ok);
    assert(ship.has_hull(kOffHull));
    assert(ship.inventory().count(ItemKind::Hull) == hull - 1);

    assert(ship.place(ItemKind::Junction, kOffHull) == BuildResult::Ok);
    assert(ship.building_at(kOffHull)->item_kind() == ItemKind::Junction);
    assert(ship.inventory().count(ItemKind::Junction) == junctions - 1);
}

void test_rejections() {
    Ship ship = stocked_ship();
    assert(ship.place(ItemKind::TransportBelt, kCenter) == BuildResult::Occupied);
    assert(ship.place(ItemKind::TransportBelt, kOnHull) == BuildResult::Ok);
    assert(ship.place(ItemKind::Junction, kOnHull) == BuildResult::Occupied);
    assert(ship.place(ItemKind::Hull, kOnHull) == BuildResult::HullAlreadyThere);
    assert(ship.place(ItemKind::Hull, {-1, 0}) == BuildResult::OutOfBounds);
    assert(ship.place(ItemKind::Hull, {0, kShipGridSize}) == BuildResult::OutOfBounds);
    assert(ship.remove_building({kShipGridSize, 0}) == BuildResult::OutOfBounds);
    assert(ship.remove_hull({0, -1}) == BuildResult::OutOfBounds);
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
    const int hull = ship.inventory().count(ItemKind::Hull);

    assert(ship.place(ItemKind::AsteroidCollector, kOnHull) == BuildResult::Ok);
    // Hull can't come out from under a building.
    assert(ship.remove_hull(kOnHull) == BuildResult::HullNotEmpty);
    assert(ship.has_hull(kOnHull));
    // The building comes off alone; the hull stays...
    assert(ship.remove_building(kOnHull) == BuildResult::Ok);
    assert(ship.building_at(kOnHull) == nullptr);
    assert(ship.has_hull(kOnHull));
    assert(ship.inventory().count(ItemKind::AsteroidCollector) == collectors);
    assert(ship.inventory().count(ItemKind::Hull) == hull);
    assert(ship.remove_building(kOnHull) == BuildResult::NothingThere);
    // ...then the bare hull.
    assert(ship.remove_hull(kOnHull) == BuildResult::Ok);
    assert(!ship.has_hull(kOnHull));
    assert(ship.inventory().count(ItemKind::Hull) == hull + 1);
    assert(ship.remove_hull(kOnHull) == BuildResult::NothingThere);

    assert(ship.remove_building(kCenter) == BuildResult::NotRemovable);
    assert(ship.remove_hull(kCenter) == BuildResult::HullNotEmpty);
    assert(ship.building_at(kCenter) == &ship.get_bridge());
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

    // On the starting hull's last column, a collector would stick out past it.
    const GridPos edge{kHullEnd, kCenter.y};
    assert(ship.place(ItemKind::AsteroidCollector, edge) == BuildResult::NoHull);
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
    // Every rotatable building is square, so turning one leaves its
    // footprint alone.
    for (int i = 0; i < kItemKindCount; ++i) {
        const ItemKind kind = static_cast<ItemKind>(i);
        if (is_placeable(kind) && is_rotatable(kind))
            assert(footprint_of(kind, Direction::East) == footprint_of(kind, Direction::North));
    }
    // Not rotatable: facing doesn't matter.
    assert(footprint_of(ItemKind::IonThruster, Direction::East) == (GridSize{2, 3}));

    assert(ship.place(ItemKind::BeltTunnel, kOnHull, Direction::East) == BuildResult::Ok);
    assert(ship.building_at({kOnHull.x, kOnHull.y + 1}) == nullptr);
    assert(ship.building_at(kOnHull)->footprint() == (GridSize{1, 1}));
}

void test_reinforced_hull() {
    Ship ship = stocked_ship();
    ship.inventory().add(ItemKind::Refinery, 1);
    ship.inventory().add(ItemKind::ReinforcedHull, 9);
    // A 3x3 patch of plain hull just east of the start hull.
    const GridPos corner = kOffHull;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            assert(ship.place(ItemKind::Hull, {corner.x + x, corner.y + y}) == BuildResult::Ok);
    const int hull = ship.inventory().count(ItemKind::Hull);

    assert(ship.place(ItemKind::Refinery, corner) == BuildResult::NeedsReinforcedHull);
    // Upgrading plain hull refunds it.
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 2; ++x)
            assert(ship.place(ItemKind::ReinforcedHull, {corner.x + x, corner.y + y}) == BuildResult::Ok);
    assert(ship.inventory().count(ItemKind::Hull) == hull + 6);
    assert(ship.hull_at(corner) == ItemKind::ReinforcedHull);
    assert(ship.place(ItemKind::ReinforcedHull, corner) == BuildResult::HullAlreadyThere);
    assert(ship.place(ItemKind::Refinery, corner) == BuildResult::NeedsReinforcedHull);
    for (int y = 0; y < 3; ++y)
        assert(ship.place(ItemKind::ReinforcedHull, {corner.x + 2, corner.y + y}) == BuildResult::Ok);
    assert(ship.place(ItemKind::Refinery, corner) == BuildResult::Ok);

    // Smaller buildings stand on it too, and it refunds as reinforced.
    assert(ship.remove_building(corner) == BuildResult::Ok);
    assert(ship.place(ItemKind::Junction, corner) == BuildResult::Ok);
    assert(ship.remove_building(corner) == BuildResult::Ok);
    const int reinforced = ship.inventory().count(ItemKind::ReinforcedHull);
    assert(ship.remove_hull(corner) == BuildResult::Ok);
    assert(ship.inventory().count(ItemKind::ReinforcedHull) == reinforced + 1);
}

}  // namespace

int main() {
    test_new_game();
    test_needs_hull();
    test_rejections();
    test_empty_inventory();
    test_remove_refunds();
    test_belt_direction();
    test_footprints();
    test_hierarchy();
    test_rotated_footprint();
    test_reinforced_hull();
    std::puts("test_ship: all passed");
    return 0;
}
