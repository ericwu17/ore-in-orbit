// test_sectors.cpp — headless tests for the star map and travel: the map a
// seed makes (the same every time, all of it reachable, home safe), what
// the player knows of it, survey scanners, charging the layout's ion
// thrusters with fuel cells, jumping, and collectors gathering at their
// sector's yields.
#include "sim/building.h"
#include "sim/fleet.h"
#include "sim/sector.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"
#include "sim/world.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <set>
#include <string>

namespace {

// The starting ship's two ion thrusters, where its layout has them.
const GridPos kThrusterA = layout_to_grid(starter_layout(), starter_layout().engines.at(0));
const GridPos kThrusterB = layout_to_grid(starter_layout(), starter_layout().engines.at(1));
// Room on the starting hull, away from the bridge, for 2x2 buildings.
const GridPos kScanner = layout_to_grid(starter_layout(), {2, 10});
const GridPos kCollectorA = layout_to_grid(starter_layout(), {2, 14});
const GridPos kCollectorB = layout_to_grid(starter_layout(), {2, 18});

void run(World& world, Tick ticks) {
    for (Tick t = 0; t < ticks; ++t)
        world.advance_tick();
}

void charge(Ship& ship, GridPos thruster) {
    while (ship.insert_item(thruster, {thruster.x, thruster.y - 1}, ItemKind::FuelCell)) {
    }
}

// Every sector reachable from home, following links.
bool connected(const StarMap& map) {
    std::vector<bool> reached(static_cast<std::size_t>(map.size()), false);
    std::vector<int> stack{0};
    reached[0] = true;
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        for (const int n : map.at(i).neighbors)
            if (!reached[static_cast<std::size_t>(n)]) {
                reached[static_cast<std::size_t>(n)] = true;
                stack.push_back(n);
            }
    }
    return std::find(reached.begin(), reached.end(), false) == reached.end();
}

void test_generate() {
    for (std::uint64_t seed = 1; seed <= 50; ++seed) {
        const StarMap map = StarMap::generate(seed);
        assert(map.size() == kStarMapSize);
        assert(connected(map));
        std::set<std::string> names;
        for (int i = 0; i < map.size(); ++i) {
            const Sector& s = map.at(i);
            assert(names.insert(s.name).second);
            assert(!s.neighbors.empty());
            for (const int n : s.neighbors) {
                assert(n != i && map.linked(n, i));
                assert(std::count(s.neighbors.begin(), s.neighbors.end(), n) == 1);
            }
            assert(s.threat >= 0 && s.threat <= kMaxThreat);
            int present = 0;
            for (const int y : s.yields) {
                assert(y >= 0);
                present += y > 0 ? 1 : 0;
            }
            assert(present >= 2);
            // Only home is visited; the others are surveyed only when visited.
            assert(s.visited == (i == 0) && s.surveyed == (i == 0));
        }
        // Home: safe, everything at 100%; its neighbors an easy first step.
        const Sector& home = map.at(0);
        assert(home.name == "Home" && home.threat == 0);
        for (const int y : home.yields)
            assert(y == 100);
        for (const int n : home.neighbors)
            assert(map.at(n).threat <= 1);
    }
    // The same seed, the same map; another seed, another.
    const StarMap a = StarMap::generate(7);
    const StarMap b = StarMap::generate(7);
    const StarMap c = StarMap::generate(8);
    bool differs = false;
    for (int i = 0; i < a.size(); ++i) {
        assert(a.at(i).name == b.at(i).name && a.at(i).x == b.at(i).x && a.at(i).neighbors == b.at(i).neighbors);
        differs = differs || a.at(i).name != c.at(i).name;
    }
    assert(differs);
    // Somewhere out there is dangerous.
    bool hostile = false;
    for (const Sector& s : a.all())
        hostile = hostile || s.threat >= 3;
    assert(hostile);
}

// Known: visited, or next to a visited sector.
void test_known() {
    const World world;
    const StarMap& map = world.get_map();
    for (int i = 0; i < map.size(); ++i)
        assert(map.known(i) == (i == 0 || map.linked(0, i)));
}

// A survey scanner surveys the sectors next to the ship's, once a second.
void test_scanner() {
    World world;
    const StarMap& map = world.get_map();
    const int next_door = map.at(0).neighbors.front();
    world.flagship().inventory().add(ItemKind::SurveyScanner);
    run(world, kTicksPerSecond);
    assert(!map.at(next_door).surveyed);
    assert(world.flagship().place(ItemKind::SurveyScanner, kScanner) == BuildResult::Ok);
    run(world, kTicksPerSecond);
    for (const int n : map.at(0).neighbors)
        assert(map.at(n).surveyed && !map.at(n).visited);
}

// Thrusters take fuel cells from belts until charged.
void test_charging() {
    World world;
    Ship& ship = world.flagship();
    ship.inventory().add(ItemKind::TransportBelt, 1);
    const GridPos belt{kThrusterA.x, kThrusterA.y - 1};
    assert(ship.place(ItemKind::TransportBelt, belt, Direction::South) == BuildResult::Ok);
    const auto* thruster = dynamic_cast<const Thruster*>(ship.building_at(kThrusterA));
    assert(thruster && thruster->get_fuel() == 0 && !thruster->charged());
    assert(!ship.insert_item(kThrusterA, belt, ItemKind::Carbon));

    for (int i = 0; i < kThrusterCharge + 3; ++i) {
        ship.insert_item(belt, {belt.x, belt.y - 1}, ItemKind::FuelCell);
        run(world, kTicksPerSecond);
    }
    assert(thruster->charged() && thruster->get_fuel() == kThrusterCharge);
    // The rest wait on the belt.
    assert(!dynamic_cast<const Conveyor*>(ship.building_at(belt))->get_items().empty());
    // Part of the layout: it stays, fuel and all.
    assert(ship.remove_building(kThrusterA) == BuildResult::NotRemovable);
    assert(thruster->get_fuel() == kThrusterCharge);
}

void test_jump() {
    World world;
    const StarMap& map = world.get_map();
    Ship& ship = world.flagship();
    const int id = world.get_fleet().flagship().id;
    const int next_door = map.at(0).neighbors.front();
    int far = -1;
    for (int i = 1; i < map.size() && far < 0; ++i)
        if (!map.linked(0, i))
            far = i;
    assert(far > 0);

    assert(world.jump(99, next_door) == JumpResult::NoSuchShip);
    assert(world.jump(id, map.size()) == JumpResult::NoSuchSector);
    assert(world.jump(id, far) == JumpResult::NotLinked);
    assert(world.jump(id, 0) == JumpResult::NotLinked);  // already there

    // Every engine the layout has needs charging.
    assert(ship.thrusters().size() == 2);
    assert(world.jump(id, next_door) == JumpResult::NotCharged);
    charge(ship, kThrusterA);
    assert(world.jump(id, next_door) == JumpResult::NotCharged);
    charge(ship, kThrusterB);
    assert(world.can_jump(id, next_door) == JumpResult::Ok);

    // One knocked offline grounds the ship till it mends.
    Building& engine = *ship.building_anchored_at(kThrusterB);
    assert(engine.take_damage(engine.max_hp()));
    assert(world.jump(id, next_door) == JumpResult::EngineOffline);
    engine.repair(1);
    assert(world.can_jump(id, next_door) == JumpResult::Ok);

    assert(world.jump(id, next_door) == JumpResult::Ok);
    assert(world.get_fleet().flagship().sector == next_door);
    assert(map.at(next_door).visited && map.at(next_door).surveyed);
    for (const Thruster* t : ship.thrusters())
        assert(t->get_fuel() == 0);
    assert(ship.get_yields() == map.at(next_door).yields);
    // Its neighbors are known now.
    for (const int n : map.at(next_door).neighbors)
        assert(map.known(n));
    // Empty, they need charging again.
    assert(world.jump(id, 0) == JumpResult::NotCharged);
}

// Collectors gather at their sector's yield of their target: faster where
// it's rich, not at all where there's none.
void test_yields() {
    World world;
    Ship& ship = world.flagship();
    ship.inventory().add(ItemKind::AsteroidCollector);
    const GridPos at = kCollectorA;
    assert(ship.place(ItemKind::AsteroidCollector, at) == BuildResult::Ok);
    assert(ship.set_collector_target(at, ItemKind::IronOre));
    const auto* collector = dynamic_cast<const Collector*>(ship.building_at(at));
    const Tick interval = collector->get_spec().interval;
    assert(collector->get_yield() == 100);

    Yields yields = ship.get_yields();
    yields[static_cast<std::size_t>(*raw_resource_index(ItemKind::IronOre))] = 0;
    yields[static_cast<std::size_t>(*raw_resource_index(ItemKind::Carbon))] = 250;
    ship.set_yields(yields);
    assert(collector->get_yield() == 0 && collector->rate_per_second() == 0.0);
    run(world, interval * 3);
    assert(collector->stored() == 0);

    // Re-aimed at carbon: 2.5 times as fast (5 in two intervals).
    assert(ship.set_collector_target(at, ItemKind::Carbon));
    assert(collector->get_yield() == 250);
    run(world, interval * 2);
    assert(collector->items().count(ItemKind::Carbon) == 5);

    // A new collector picks the ship's yields up when placed.
    ship.inventory().add(ItemKind::AsteroidCollector);
    const GridPos other = kCollectorB;
    assert(ship.place(ItemKind::AsteroidCollector, other) == BuildResult::Ok);
    assert(dynamic_cast<const Collector*>(ship.building_at(other))->get_yield() == 0);  // aimed at iron
}

// A ship launched from another is in its sector, and gathers at its yields.
void test_launch_in_sector() {
    World world;
    Fleet& fleet = world.get_fleet();
    Yields yields = world.flagship().get_yields();
    yields[0] = 175;
    world.flagship().set_yields(yields);
    world.flagship().inventory().add(ItemKind::ShipStarterPack);
    world.flagship().inventory().add(starter_layout().cost);
    int id = 0;
    assert(fleet.launch(fleet.flagship().id, starter_layout(), &id) == FleetResult::Ok);
    assert(fleet.find(id)->ship.get_yields() == yields);
}

}  // namespace

int main() {
    test_generate();
    test_known();
    test_scanner();
    test_charging();
    test_jump();
    test_yields();
    test_launch_in_sector();
    std::puts("test_sectors: all passed");
    return 0;
}
