// test_ship.cpp — headless tests for the ship build grid: the starting ship,
// placement rules, multi-cell footprints, the bridge inventory and removal
// refunds.
#include "sim/building.h"
#include "sim/ship.h"
#include "sim/world.h"

#include <cassert>
#include <cstdio>

namespace {

constexpr GridPos kCenter{kShipGridSize / 2, kShipGridSize / 2};
// Just outside the 5x5 starting hull patch.
constexpr GridPos kOffHull{kCenter.x + 3, kCenter.y};
// On the starting hull, next to the bridge.
constexpr GridPos kOnHull{kCenter.x + 1, kCenter.y};

void test_new_game() {
    World world;
    const Ship& ship = world.get_ship();
    const Building* bridge = ship.building_at(kCenter);
    assert(bridge == &ship.get_bridge());
    assert(!bridge->item_kind());
    assert(ship.has_hull(kCenter));
    assert(ship.has_hull({kCenter.x - 2, kCenter.y - 2}));
    assert(ship.has_hull({kCenter.x + 2, kCenter.y + 2}));
    assert(!ship.has_hull(kOffHull));
    for (int i = 0; i < kItemKindCount; ++i)
        assert(ship.inventory().count(static_cast<ItemKind>(i)) > 0);
    for (int i = 0; i < 10; ++i)
        world.advance_tick();
    assert(world.get_tick() == 10);
}

void test_needs_hull() {
    Ship ship = Ship::new_game();
    const int pipes = ship.inventory().count(ItemKind::Pipe);
    const int hull = ship.inventory().count(ItemKind::Hull);

    assert(ship.place(ItemKind::Pipe, kOffHull) == BuildResult::NoHull);
    assert(ship.inventory().count(ItemKind::Pipe) == pipes);

    assert(ship.place(ItemKind::Hull, kOffHull) == BuildResult::Ok);
    assert(ship.has_hull(kOffHull));
    assert(ship.inventory().count(ItemKind::Hull) == hull - 1);

    assert(ship.place(ItemKind::Pipe, kOffHull) == BuildResult::Ok);
    assert(ship.building_at(kOffHull)->item_kind() == ItemKind::Pipe);
    assert(ship.inventory().count(ItemKind::Pipe) == pipes - 1);
}

void test_rejections() {
    Ship ship = Ship::new_game();
    assert(ship.place(ItemKind::TransportBelt, kCenter) == BuildResult::Occupied);
    assert(ship.place(ItemKind::TransportBelt, kOnHull) == BuildResult::Ok);
    assert(ship.place(ItemKind::Pipe, kOnHull) == BuildResult::Occupied);
    assert(ship.place(ItemKind::Hull, kOnHull) == BuildResult::HullAlreadyThere);
    assert(ship.place(ItemKind::Hull, {-1, 0}) == BuildResult::OutOfBounds);
    assert(ship.place(ItemKind::Hull, {0, kShipGridSize}) == BuildResult::OutOfBounds);
    assert(ship.remove_building({kShipGridSize, 0}) == BuildResult::OutOfBounds);
    assert(ship.remove_hull({0, -1}) == BuildResult::OutOfBounds);
}

void test_empty_inventory() {
    Ship ship = Ship::new_game();
    while (ship.inventory().take(ItemKind::TransportBelt)) {
    }
    assert(ship.can_place(ItemKind::TransportBelt, kOnHull) == BuildResult::NoneInInventory);
    assert(ship.place(ItemKind::TransportBelt, kOnHull) == BuildResult::NoneInInventory);
    assert(ship.building_at(kOnHull) == nullptr);
    assert(ship.inventory().count(ItemKind::TransportBelt) == 0);
}

void test_remove_refunds() {
    Ship ship = Ship::new_game();
    const int collectors = ship.inventory().count(ItemKind::HydrogenCollector);
    const int hull = ship.inventory().count(ItemKind::Hull);

    assert(ship.place(ItemKind::HydrogenCollector, kOnHull) == BuildResult::Ok);
    // Hull can't come out from under a building.
    assert(ship.remove_hull(kOnHull) == BuildResult::HullNotEmpty);
    assert(ship.has_hull(kOnHull));
    // The building comes off alone; the hull stays...
    assert(ship.remove_building(kOnHull) == BuildResult::Ok);
    assert(ship.building_at(kOnHull) == nullptr);
    assert(ship.has_hull(kOnHull));
    assert(ship.inventory().count(ItemKind::HydrogenCollector) == collectors);
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
    Ship ship = Ship::new_game();
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
    Ship ship = Ship::new_game();
    assert(footprint_of(ItemKind::AsteroidCollector) == (GridSize{2, 2}));
    assert(footprint_of(ItemKind::Pipe) == (GridSize{1, 1}));

    // The starting hull spans kCenter +-2: a collector at (c+2, c) would
    // stick out past it.
    const GridPos edge{kCenter.x + 2, kCenter.y};
    assert(ship.place(ItemKind::AsteroidCollector, edge) == BuildResult::NoHull);
    // Overlapping the bridge, even by one cell.
    assert(ship.place(ItemKind::AsteroidCollector, {kCenter.x - 1, kCenter.y - 1}) == BuildResult::Occupied);
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
    assert(ship.place(ItemKind::Pipe, {kOnHull.x + 1, kOnHull.y + 1}) == BuildResult::Occupied);

    // Removing from any covered cell removes the whole thing, once.
    assert(ship.remove_building({kOnHull.x + 1, kOnHull.y + 1}) == BuildResult::Ok);
    assert(ship.building_anchored_at(kOnHull) == nullptr);
    assert(ship.inventory().count(ItemKind::AsteroidCollector) == collectors);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            assert(ship.building_at({kOnHull.x + x, kOnHull.y + y}) == nullptr);
            assert(ship.has_hull({kOnHull.x + x, kOnHull.y + y}));
        }
    assert(ship.place(ItemKind::Pipe, {kOnHull.x + 1, kOnHull.y + 1}) == BuildResult::Ok);
}

void test_hierarchy() {
    for (const ItemKind kind : {ItemKind::HydrogenCollector, ItemKind::AsteroidCollector}) {
        const auto b = make_building(kind, Direction::North);
        assert(dynamic_cast<const Collector*>(b.get()) != nullptr);
        assert(b->item_kind() == kind);
        assert(b->footprint() == footprint_of(kind));
    }
    const auto pipe = make_building(ItemKind::Pipe, Direction::East);
    assert(dynamic_cast<const Collector*>(pipe.get()) == nullptr);
    assert(!pipe->rotatable());
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
    std::puts("test_ship: all passed");
    return 0;
}
