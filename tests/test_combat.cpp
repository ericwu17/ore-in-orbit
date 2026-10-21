// test_combat.cpp — headless tests for combat: turrets firing what's fed to
// them (and nothing without it), enemies closing in on the nearest building
// and wearing it down, buildings destroyed for nothing, mending while no
// enemy is near, waves and clearing a sector, losing ships and the fleet,
// and the same seed fighting the same fight.
#include "sim/building.h"
#include "sim/combat.h"
#include "sim/fleet.h"
#include "sim/sector.h"
#include "sim/ship.h"
#include "sim/world.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

// East of the bridge, on the starting hull: a 2x2 turret, and its middle.
constexpr GridPos kTurret{72, 64};
constexpr float kTurretX = 73.0f;
constexpr float kTurretY = 65.0f;

Enemy drone_at(float x, float y) {
    Enemy e;
    e.kind = EnemyKind::Drone;
    e.x = x;
    e.y = y;
    e.hp = enemy_spec(EnemyKind::Drone).hp;
    return e;
}

// A safe sector: no waves of its own.
Sector calm() {
    Sector s;
    s.yields.fill(100);
    return s;
}

void fight(Ship& ship, Battle& battle, Sector& sector, Rng& rng, Tick& now, Tick ticks) {
    for (Tick t = 0; t < ticks; ++t) {
        ++now;
        ship.tick();
        combat_tick(ship, battle, sector, rng, now);
    }
}

Turret& place_autocannon(Ship& ship, int magazines) {
    ship.inventory().add(ItemKind::Autocannon);
    assert(ship.place(ItemKind::Autocannon, kTurret) == BuildResult::Ok);
    for (int i = 0; i < magazines; ++i)
        assert(ship.insert_item(kTurret, {kTurret.x, kTurret.y - 1}, ItemKind::SlugMagazine));
    return *dynamic_cast<Turret*>(ship.building_anchored_at(kTurret));
}

void test_turret_needs_ammo() {
    Ship ship = Ship::new_game();
    Sector sector = calm();
    Rng rng{1};
    Tick now = 0;
    Turret& turret = place_autocannon(ship, 0);
    assert(!ship.insert_item(kTurret, {kTurret.x, kTurret.y - 1}, ItemKind::RailSlug));  // the wrong kind
    Battle battle;
    battle.enemies.push_back(drone_at(kTurretX + 5.0f, kTurretY));
    fight(ship, battle, sector, rng, now, 2 * kTicksPerSecond);
    assert(battle.enemies.size() == 1 && battle.enemies[0].hp == enemy_spec(EnemyKind::Drone).hp);
    assert(turret.fired_at == 0);
}

// A drone takes four autocannon shots; the magazine they came from keeps the
// rest.
void test_turret_kills() {
    Ship ship = Ship::new_game();
    Sector sector = calm();
    Rng rng{1};
    Tick now = 0;
    Turret& turret = place_autocannon(ship, 2);
    const TurretSpec& spec = turret.get_spec();
    assert(turret.shots_left() == 2 * spec.shots_per_ammo);
    Battle battle;
    battle.enemies.push_back(drone_at(kTurretX + 5.0f, kTurretY));
    // Out of range: it waits.
    battle.enemies.push_back(drone_at(kTurretX - 40.0f, kTurretY - 40.0f));
    fight(ship, battle, sector, rng, now, 3 * spec.interval + 1);
    assert(battle.enemies.size() == 1);
    const int shots = (enemy_spec(EnemyKind::Drone).hp + spec.damage - 1) / spec.damage;
    assert(turret.get_ammo() == 1 && turret.get_shots() == spec.shots_per_ammo - shots);
    assert(turret.fired_at > 0);
}

// An enemy makes for the nearest building, shoots it once in range, and
// destroys it: nothing comes back, neither it nor what it held. Then it
// moves on to the next.
void test_enemy_attacks() {
    Ship ship = Ship::new_game();
    Sector sector = calm();
    Rng rng{1};
    Tick now = 0;
    const GridPos belt{80, 66};
    assert(ship.place(ItemKind::TransportBelt, belt, Direction::West) == BuildResult::Ok);
    assert(ship.insert_item(belt, {81, 66}, ItemKind::Gear));
    const int belts = ship.inventory().count(ItemKind::TransportBelt);
    const int gears = ship.inventory().count(ItemKind::Gear);

    Battle battle;
    battle.enemies.push_back(drone_at(100.0f, 66.5f));
    // It closes in: 20 cells at its speed, then within range.
    fight(ship, battle, sector, rng, now, kTicksPerSecond);
    assert(battle.enemies[0].x < 100.0f && battle.enemies[0].target == belt);
    for (Tick t = 0; t < 30 * kTicksPerSecond && ship.building_at(belt); ++t)
        fight(ship, battle, sector, rng, now, 1);
    assert(!ship.building_at(belt));
    assert(ship.inventory().count(ItemKind::TransportBelt) == belts);
    assert(ship.inventory().count(ItemKind::Gear) == gears);

    // Next: the bridge, the only building left.
    fight(ship, battle, sector, rng, now, 10 * kTicksPerSecond);
    assert(battle.enemies[0].target == kStartBridgePos);
    assert(ship.get_bridge().damaged());
}

// Buildings mend while no enemy is on their ship's grid.
void test_repair() {
    World world;
    Ship& ship = world.flagship();
    Bridge& bridge = ship.get_bridge();
    const int max = bridge.max_hp();
    assert(!bridge.take_damage(500));
    for (Tick t = 0; t < kTicksPerSecond; ++t)
        world.advance_tick();
    assert(bridge.get_hp() == max - 500 + max * kRepairPercent / 100);

    // Not while enemies are there.
    FleetShip& flagship = world.get_fleet().flagship();
    flagship.battle.enemies.push_back(drone_at(-30.0f, -30.0f));  // far off: it won't arrive yet
    const int hp = bridge.get_hp();
    for (Tick t = 0; t < kTicksPerSecond; ++t)
        world.advance_tick();
    assert(bridge.get_hp() == hp);
}

// A hostile sector's waves: the first kFirstWaveDelay after arriving, then
// one every kWaveInterval, until it has sent them all and is cleared.
void test_waves() {
    Ship ship = Ship::new_game();
    Sector sector = calm();
    sector.threat = 1;
    sector.waves_left = 2;
    assert(sector.hostile() && !sector.cleared());
    Rng rng{1};
    Tick now = 0;
    Battle battle;
    fight(ship, battle, sector, rng, now, 1);
    assert(battle.next_wave == now + kFirstWaveDelay);
    fight(ship, battle, sector, rng, now, kFirstWaveDelay - 1);
    assert(battle.enemies.empty());
    fight(ship, battle, sector, rng, now, 1);
    // Threat 1: drones only, all coming from one side, outside the hull.
    assert(!battle.enemies.empty() && sector.waves_left == 1);
    const std::size_t first = battle.enemies.size();
    for (const Enemy& e : battle.enemies) {
        assert(e.kind == EnemyKind::Drone);
        const float d = std::hypot(e.x - 64.0f, e.y - 64.0f);
        assert(d > static_cast<float>(kStartHullSide) * 0.5f);
    }
    assert(battle.next_wave == now + kWaveInterval);
    battle.enemies.clear();
    fight(ship, battle, sector, rng, now, kWaveInterval);
    // The second is bigger, and the last.
    assert(battle.enemies.size() > first);
    assert(sector.waves_left == 0 && !battle.next_wave);
    assert(!sector.hostile() && sector.cleared());
}

// Heavier sectors send heavier ships.
void test_wave_mix() {
    const Ship ship = Ship::new_game();
    Rng rng{3};
    bool gunship = false;
    bool dreadnought = false;
    for (int i = 0; i < 20; ++i) {
        Battle battle;
        spawn_wave(ship, battle, kMaxThreat, 6, rng);
        for (const Enemy& e : battle.enemies) {
            gunship = gunship || e.kind == EnemyKind::Gunship;
            dreadnought = dreadnought || e.kind == EnemyKind::Dreadnought;
        }
    }
    assert(gunship && dreadnought);
    for (int i = 0; i < 20; ++i) {
        Battle battle;
        spawn_wave(ship, battle, 1, 3, rng);
        for (const Enemy& e : battle.enemies)
            assert(e.kind == EnemyKind::Drone);
    }
}

// A ship whose bridge falls is lost, cargo on its way to it too; with the
// last one, the fleet, and the World stops.
void test_losing() {
    World world;
    Fleet& fleet = world.get_fleet();
    world.flagship().inventory().add({{ItemKind::ShipStarterPack, 1}, {ItemKind::CargoPod, 1}, {ItemKind::Gear, 5}});
    int second = 0;
    assert(fleet.launch(fleet.flagship().id, &second) == FleetResult::Ok);
    const int first = fleet.flagship().id;

    FleetShip& doomed = *fleet.find(second);
    assert(fleet.send(first, second, {{ItemKind::Gear, 5}}, world.get_tick()) == FleetResult::Ok);
    assert(!doomed.ship.get_bridge().take_damage(doomed.ship.get_bridge().max_hp() - 1));
    doomed.battle.enemies.push_back(drone_at(68.0f, 64.0f));  // right beside it
    for (int t = 0; t < 2 * static_cast<int>(kTicksPerSecond) && fleet.find(second); ++t)
        world.advance_tick();
    assert(!fleet.find(second) && fleet.get_ships().size() == 1);
    for (Tick t = 0; t < kTransferTicks; ++t)
        world.advance_tick();
    assert(fleet.in_flight().empty());
    assert(!world.fleet_lost());

    Ship& last = world.flagship();
    assert(!last.get_bridge().take_damage(last.get_bridge().max_hp() - 1));
    fleet.flagship().battle.enemies.push_back(drone_at(68.0f, 64.0f));
    for (int t = 0; t < 2 * static_cast<int>(kTicksPerSecond) && !world.fleet_lost(); ++t)
        world.advance_tick();
    assert(world.fleet_lost());
    const Tick end = world.get_tick();
    world.advance_tick();
    assert(world.get_tick() == end);
}

// Jumping away leaves the enemies behind.
void test_retreat() {
    World world;
    Ship& ship = world.flagship();
    const int id = world.get_fleet().flagship().id;
    assert(world.send_wave(id));
    assert(world.get_fleet().flagship().battle.under_attack());
    ship.inventory().add(ItemKind::IonThruster, 2);
    for (const GridPos p : {GridPos{44, 44}, GridPos{46, 44}}) {
        assert(ship.place(ItemKind::IonThruster, p) == BuildResult::Ok);
        while (ship.insert_item(p, {p.x, p.y - 1}, ItemKind::FuelCell)) {
        }
    }
    assert(world.jump(id, world.get_map().at(0).neighbors.front()) == JumpResult::Ok);
    assert(!world.get_fleet().flagship().battle.under_attack());
}

// The same seed, the same fight.
void test_deterministic() {
    World a(42);
    World b(42);
    for (World* w : {&a, &b}) {
        assert(w->send_wave(w->get_fleet().flagship().id));
        for (Tick t = 0; t < 20 * kTicksPerSecond; ++t)
            w->advance_tick();
    }
    const std::vector<Enemy>& ea = a.get_fleet().flagship().battle.enemies;
    const std::vector<Enemy>& eb = b.get_fleet().flagship().battle.enemies;
    assert(!ea.empty() && ea.size() == eb.size());
    for (std::size_t i = 0; i < ea.size(); ++i)
        assert(ea[i].x == eb[i].x && ea[i].y == eb[i].y && ea[i].hp == eb[i].hp);
    assert(a.flagship().get_bridge().get_hp() == b.flagship().get_bridge().get_hp());
}

void test_ids() {
    for (int i = 0; i < kEnemyKindCount; ++i) {
        const EnemyKind kind = static_cast<EnemyKind>(i);
        assert(enemy_from_id(enemy_id(kind)) == kind);
        assert(enemy_spec(kind).hp > 0 && enemy_spec(kind).cost > 0);
    }
    assert(!enemy_from_id("Kraken"));
    assert(waves_for_threat(0) == 0 && waves_for_threat(1) > 0);
}

}  // namespace

int main() {
    test_ids();
    test_turret_needs_ammo();
    test_turret_kills();
    test_enemy_attacks();
    test_repair();
    test_waves();
    test_wave_mix();
    test_losing();
    test_retreat();
    test_deterministic();
    std::puts("test_combat: all passed");
    return 0;
}
