// test_sectors.cpp — headless tests for the star map and travel: the map a
// seed makes (the same every time, all of it reachable, home and the
// sectors around it safe), what the map shows, charging the layout's ion
// thrusters with fuel cells, jumping, collectors gathering, and running a
// sector's resources out.
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
const GridPos kCollectorA = layout_to_grid(starter_layout(), {2, 14});
const GridPos kCollectorB = layout_to_grid(starter_layout(), {2, 18});

void run(World& world, Tick ticks) {
    for (Tick t = 0; t < ticks; ++t)
        world.advance_tick();
}

// Charges the thrusters from the bridge, as a tick would.
void charge(Ship& ship) {
    ship.inventory().add(ItemKind::FuelCell, kThrusterCharge * static_cast<int>(ship.thrusters().size()));
    ship.refuel_thrusters();
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
            // An enemy only where there's a threat, and one of those found at it.
            if (s.enemy) {
                const std::vector<EnemyClass> fits = enemies_for_threat(s.threat);
                assert(std::find(fits.begin(), fits.end(), *s.enemy) != fits.end());
            }
            assert(!s.beaten);
            int present = 0;
            for (std::size_t r = 0; r < s.reserves.size(); ++r) {
                assert(s.reserves[r] == 0 || s.reserves[r] == (i == 0 ? kHomeReserves : kSectorReserves)[r]);
                present += s.reserves[r] > 0 ? 1 : 0;
            }
            assert(present >= 2);
            // Only home is visited.
            assert(s.visited == (i == 0));
        }
        // Home: safe, with some of everything; its neighbors and the other
        // safe sectors too, with all of everything.
        const Sector& home = map.at(0);
        assert(home.name == "Home" && home.threat == 0);
        assert(home.reserves == kHomeReserves);
        for (const int left : home.reserves)
            assert(left > 0);
        for (const int n : home.neighbors)
            assert(map.at(n).threat == 0 && !map.at(n).enemy && map.at(n).reserves == kSectorReserves);
        int safe = 0;
        for (int i = 1; i < map.size(); ++i)
            if (map.at(i).threat == 0 && !map.at(i).enemy) {
                ++safe;
                assert(map.at(i).reserves == kSectorReserves);
            }
        assert(safe >= kSafeSectors);
        // Elsewhere, there's more of what there is than at home.
        for (std::size_t r = 0; r < kSectorReserves.size(); ++r)
            assert(kSectorReserves[r] > kHomeReserves[r]);
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
    int enemies = 0;
    int quiet = 0;
    for (const Sector& s : a.all()) {
        hostile = hostile || s.threat >= 3;
        enemies += s.enemy ? 1 : 0;
        quiet += s.threat > 0 && !s.enemy ? 1 : 0;
    }
    assert(hostile);
    // Enemy ships to find, and threatening sectors without one.
    assert(enemies >= 5 && quiet >= 1);
}

// Known: visited, or next to a visited sector.
void test_known() {
    const World world;
    const StarMap& map = world.get_map();
    for (int i = 0; i < map.size(); ++i)
        assert(map.known(i) == (i == 0 || map.linked(0, i)));
}

// The map shows the known sectors, with what's there, and those taking an
// open delivery, however far; revealed (the debug menu), it shows them all.
void test_shown() {
    World world;
    const StarMap& map = world.get_map();
    const Progress& progress = world.get_fleet().get_progress();
    const auto takes_open = [&](int i) {
        const std::vector<std::size_t> here = map.deliveries_at(i);
        return std::any_of(here.begin(), here.end(), [&](std::size_t d) { return progress.open(d); });
    };
    int hidden = 0;
    for (int i = 0; i < map.size(); ++i) {
        assert(world.shown(i) == (map.known(i) || takes_open(i)));
        hidden += world.shown(i) ? 0 : 1;
    }
    assert(hidden > 0);
    for (const int n : map.at(0).neighbors)
        assert(world.shown(n) && !map.at(n).visited);
    // A later delivery's sector, out of sight, shows once it opens.
    Progress& p = world.get_fleet().get_progress();
    const auto far = std::find_if(all_deliveries().begin(), all_deliveries().end(), [&](const Delivery& d) {
        return !map.known(map.delivery_sector(delivery_index(d.name)));
    });
    assert(far != all_deliveries().end());
    const std::size_t later = delivery_index(far->name);
    assert(!world.shown(map.delivery_sector(later)));
    while (!p.open(later))
        assert(p.complete(p.open_deliveries().front()));
    assert(world.shown(map.delivery_sector(later)));
    world.reveal_map();
    for (int i = 0; i < map.size(); ++i)
        assert(world.shown(i));
}

// A new game starts charged, with fuel for three more jumps in the bridge.
// Thrusters draw fuel cells from the bridge each tick, until charged, and
// take none from belts.
void test_charging() {
    World world;
    Ship& ship = world.flagship();
    const int per_jump = kThrusterCharge * static_cast<int>(ship.thrusters().size());
    for (const Thruster* t : ship.thrusters())
        assert(t->charged());
    assert(ship.inventory().count(ItemKind::FuelCell) == 3 * per_jump);

    const auto* thruster = dynamic_cast<const Thruster*>(ship.building_at(kThrusterA));
    assert(thruster);
    ship.burn_thrusters();
    ship.inventory().take(ItemKind::FuelCell, ship.inventory().count(ItemKind::FuelCell));
    ship.inventory().add(ItemKind::FuelCell, kThrusterCharge + 3);
    run(world, 1);
    const auto* other = dynamic_cast<const Thruster*>(ship.building_at(kThrusterB));
    assert(thruster->get_fuel() + other->get_fuel() == kThrusterCharge + 3);
    assert(ship.inventory().count(ItemKind::FuelCell) == 0);
    // More in the bridge tops them up, and what's past full stays there.
    ship.inventory().add(ItemKind::FuelCell, kThrusterCharge);
    run(world, 1);
    assert(thruster->charged() && other->charged());
    assert(ship.inventory().count(ItemKind::FuelCell) == 3);

    // Belts can't feed them.
    ship.burn_thrusters();
    ship.inventory().take(ItemKind::FuelCell, 3);
    ship.inventory().add(ItemKind::TransportBelt, 1);
    const GridPos belt{kThrusterA.x, kThrusterA.y - 1};
    assert(ship.place(ItemKind::TransportBelt, belt, Direction::South) == BuildResult::Ok);
    assert(!ship.insert_item(kThrusterA, belt, ItemKind::FuelCell));
    // Part of the layout: it stays.
    assert(ship.remove_building(kThrusterA) == BuildResult::NotRemovable);
}

// Four jumps on the fuel a new game starts with, and no fifth.
void test_four_jumps() {
    World world;
    const int id = world.get_fleet().flagship().id;
    for (int jump = 0; jump < 4; ++jump) {
        const FleetShip& s = *world.get_fleet().find(id);
        const int to = world.get_map().at(s.sector).neighbors.front();
        assert(world.jump(id, to) == JumpResult::Ok);
        run(world, 1);
    }
    const FleetShip& s = *world.get_fleet().find(id);
    assert(world.jump(id, world.get_map().at(s.sector).neighbors.front()) == JumpResult::NotCharged);
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

    // Every engine the layout has needs charging (a new game's are).
    assert(ship.thrusters().size() == 2);
    assert(world.can_jump(id, next_door) == JumpResult::Ok);
    const int spare = ship.inventory().count(ItemKind::FuelCell);
    ship.inventory().take(ItemKind::FuelCell, spare);
    ship.burn_thrusters();
    assert(world.jump(id, next_door) == JumpResult::NotCharged);
    charge(ship);
    assert(world.can_jump(id, next_door) == JumpResult::Ok);

    // One knocked offline grounds the ship till it mends.
    Building& engine = *ship.building_anchored_at(kThrusterB);
    assert(engine.take_damage(engine.max_hp()));
    assert(world.jump(id, next_door) == JumpResult::EngineOffline);
    engine.repair(1);
    assert(world.can_jump(id, next_door) == JumpResult::Ok);

    assert(world.jump(id, next_door) == JumpResult::Ok);
    assert(world.get_fleet().flagship().sector == next_door);
    assert(map.at(next_door).visited);
    for (const Thruster* t : ship.thrusters())
        assert(t->get_fuel() == 0);
    // Its neighbors are known now.
    for (const int n : map.at(next_door).neighbors)
        assert(map.known(n));
    // Empty, they need charging again.
    assert(world.jump(id, 0) == JumpResult::NotCharged);
}

// Collectors gather at their spec's pace wherever they are, and not at all
// where there's none of their target.
void test_collector_speed() {
    World world;
    Ship& ship = world.flagship();
    ship.inventory().add(ItemKind::AsteroidCollector);
    assert(ship.place(ItemKind::AsteroidCollector, kCollectorA) == BuildResult::Ok);
    assert(ship.set_collector_target(kCollectorA, ItemKind::IronOre));
    const auto* collector = dynamic_cast<const Collector*>(ship.building_at(kCollectorA));
    const Tick interval = collector->get_spec().interval;
    assert(collector->rate_per_second() == static_cast<double>(kTicksPerSecond) / static_cast<double>(interval));
    run(world, interval * 2);
    assert(collector->items().count(ItemKind::IronOre) == 2);
    run(world, interval / 2);
    assert(collector->catch_progress() == 0.5f);

    // Where a sector has none left, a tractor array aimed at it stalls.
    world.drain_sector(0, 0);
    ship.inventory().add(ItemKind::TractorArray);
    assert(ship.place(ItemKind::TractorArray, kCollectorB) == BuildResult::Ok);
    assert(ship.set_collector_target(kCollectorB, ItemKind::IridiumOre));
    const auto* tractor = dynamic_cast<const Collector*>(ship.building_at(kCollectorB));
    run(world, tractor->get_spec().interval * 3);
    assert(tractor->stored() == 0 && tractor->is_exhausted());
}

// A ship launched from another is in its sector.
void test_launch_in_sector() {
    World world;
    Fleet& fleet = world.get_fleet();
    world.flagship().inventory().add(ItemKind::ShipStarterPack);
    world.flagship().inventory().add(starter_layout().cost);
    int id = 0;
    assert(fleet.launch(fleet.flagship().id, starter_layout(), &id) == FleetResult::Ok);
    assert(fleet.find(id)->sector == fleet.flagship().sector);
}

// A sector's resources run out: each catch takes one from its reserves,
// and once a resource is gone, collectors aimed at it stall there for good
// (and it's told, once); what else is there goes on.
void test_run_out() {
    World world;
    Ship& ship = world.flagship();
    ship.inventory().add(ItemKind::AsteroidCollector, 2);
    assert(ship.place(ItemKind::AsteroidCollector, kCollectorA) == BuildResult::Ok);
    assert(ship.place(ItemKind::AsteroidCollector, kCollectorB) == BuildResult::Ok);
    assert(ship.set_collector_target(kCollectorA, ItemKind::IronOre));
    assert(ship.set_collector_target(kCollectorB, ItemKind::Carbon));
    const auto* iron = dynamic_cast<const Collector*>(ship.building_at(kCollectorA));
    const auto* carbon = dynamic_cast<const Collector*>(ship.building_at(kCollectorB));
    const Tick interval = iron->get_spec().interval;
    const Sector& home = world.get_map().at(0);
    assert(home.left(ItemKind::IronOre) == kHomeReserves[0]);

    world.drain_sector(0, 3);
    assert(home.left(ItemKind::IronOre) == 3 && home.left(ItemKind::Carbon) == 3);
    run(world, interval * 2);
    assert(iron->stored() == 2 && home.left(ItemKind::IronOre) == 1 && !iron->is_exhausted());
    (void)world.take_events();
    run(world, interval * 5);
    // Three caught in all, the last of it, and no more.
    assert(iron->stored() == 3 && carbon->stored() == 3);
    assert(home.left(ItemKind::IronOre) == 0);
    assert(iron->is_exhausted() && carbon->is_exhausted());
    const std::vector<Event> events = world.take_events();
    const auto told = [&](ItemKind kind) {
        return std::count_if(events.begin(), events.end(), [&](const Event& e) {
            return e.kind == EventKind::ResourceRunOut && e.sector == 0 && e.item == kind;
        });
    };
    assert(told(ItemKind::IronOre) == 1 && told(ItemKind::Carbon) == 1 && told(ItemKind::Silica) == 0);
    run(world, interval * 5);
    assert(iron->stored() == 3);
    const std::vector<Event> later = world.take_events();
    assert(std::none_of(later.begin(), later.end(), [](const Event& e) { return e.kind == EventKind::ResourceRunOut; }));

    // Re-aimed at what's still there, it gathers again.
    assert(ship.set_collector_target(kCollectorA, ItemKind::Silica));
    run(world, interval * 2);
    assert(!iron->is_exhausted() && iron->items().count(ItemKind::Silica) >= 1);
    assert(home.left(ItemKind::Silica) < 3);
}

// Ships in one sector gather from the same reserves; a ship on its own
// (not in a fleet) has no end of them.
void test_shared_reserves() {
    World world;
    Fleet& fleet = world.get_fleet();
    world.flagship().inventory().add(ItemKind::ShipStarterPack);
    world.flagship().inventory().add(starter_layout().cost);
    int id = 0;
    assert(fleet.launch(fleet.flagship().id, starter_layout(), &id) == FleetResult::Ok);
    for (Ship* ship : {&world.flagship(), &fleet.find(id)->ship}) {
        ship->inventory().add(ItemKind::AsteroidCollector);
        assert(ship->place(ItemKind::AsteroidCollector, kCollectorA) == BuildResult::Ok);
        assert(ship->set_collector_target(kCollectorA, ItemKind::CopperOre));
    }
    world.drain_sector(0, 5);
    const Tick interval = collector_spec(ItemKind::AsteroidCollector).interval;
    run(world, interval * 10);
    const auto caught = [](const Ship& ship) {
        return dynamic_cast<const Collector*>(ship.building_at(kCollectorA))->stored();
    };
    assert(caught(world.flagship()) + caught(fleet.find(id)->ship) == 5);
    assert(world.get_map().at(0).left(ItemKind::CopperOre) == 0);

    Ship alone = Ship::new_game();
    alone.inventory().add(ItemKind::AsteroidCollector);
    assert(alone.place(ItemKind::AsteroidCollector, kCollectorA) == BuildResult::Ok);
    for (Tick t = 0; t < interval * 4; ++t)
        alone.tick();
    assert(caught(alone) == 4);
}

}  // namespace

int main() {
    test_generate();
    test_known();
    test_shown();
    test_charging();
    test_four_jumps();
    test_jump();
    test_collector_speed();
    test_launch_in_sector();
    test_run_out();
    test_shared_reserves();
    std::puts("test_sectors: all passed");
    return 0;
}
