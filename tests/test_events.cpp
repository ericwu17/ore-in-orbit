// test_events.cpp — headless tests for the events the World records as the
// game goes on (sim/events.h), which the UI plays sounds for: fights met,
// fought, won and lost, engines charged, jumps, cargo landing, milestones,
// and the log's cap.
#include "sim/building.h"
#include "sim/combat.h"
#include "sim/events.h"
#include "sim/fleet.h"
#include "sim/milestones.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"
#include "sim/world.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

namespace {

// Steps `world` `ticks` times, gathering every event on the way.
std::vector<Event> run(World& world, Tick ticks) {
    std::vector<Event> all;
    for (Tick t = 0; t < ticks; ++t) {
        world.advance_tick();
        for (const Event& e : world.take_events())
            all.push_back(e);
    }
    return all;
}

// How many of `events` are of `kind`, and match `also`.
template <typename Pred>
int count(const std::vector<Event>& events, EventKind kind, Pred also) {
    return static_cast<int>(
        std::count_if(events.begin(), events.end(), [&](const Event& e) { return e.kind == kind && also(e); }));
}
int count(const std::vector<Event>& events, EventKind kind) {
    return count(events, kind, [](const Event&) { return true; });
}

void feed(Ship& ship, GridPos p, ItemKind kind, int n) {
    for (int i = 0; i < n; ++i)
        if (!ship.insert_item(p, {p.x, p.y - 1}, kind))
            return;
}

// Fuels every ion thruster on `ship` full.
void charge_thrusters(Ship& ship) {
    for (const GridPos p : ship.placement_order())
        if (dynamic_cast<const Thruster*>(ship.building_anchored_at(p)))
            feed(ship, p, ItemKind::FuelCell, kThrusterCharge);
}

// The starter layout's first weapon port.
const GridPos kGun = layout_to_grid(starter_layout(), starter_layout().weapon_ports.at(0));

// A burst laser on the flagship, fed; the enemy, met; the fight, fought:
// weapons firing on both sides, shots landing, and the enemy beaten, with
// the sector where it all happened.
void test_fight() {
    World world;
    FleetShip& flagship = world.get_fleet().flagship();
    Ship& ship = flagship.ship;
    const int sector = flagship.sector;
    ship.inventory().add(ItemKind::BurstLaser);
    assert(ship.place(ItemKind::BurstLaser, kGun) == BuildResult::Ok);
    assert(world.spawn_enemy(flagship.id, EnemyClass::Scout));

    std::vector<Event> events = run(world, 1);
    assert(count(events, EventKind::EnemyEngaged, [&](const Event& e) {
               return e.sector == sector && e.ship == flagship.id;
           }) == 1);
    for (int i = 0; i < 300 * static_cast<int>(kTicksPerSecond) && world.engagement_in(sector); ++i) {
        feed(ship, kGun, ItemKind::FuelCell, kWeaponFuelCapacity);
        feed(ship, kGun, ItemKind::FocusCrystal, kWeaponAmmoCapacity);
        for (const Event& e : run(world, 1))
            events.push_back(e);
    }
    assert(!world.engagement_in(sector));
    const auto here = [&](const Event& e) { return e.sector == sector; };
    assert(count(events, EventKind::WeaponFired, [&](const Event& e) {
               return here(e) && !e.enemy && e.item == ItemKind::BurstLaser && e.ship == flagship.id;
           }) > 0);
    assert(count(events, EventKind::WeaponFired, [&](const Event& e) { return here(e) && e.enemy; }) > 0);
    const int landed_on_enemy = count(events, EventKind::ShotHit, [&](const Event& e) { return here(e) && e.enemy; }) +
                                count(events, EventKind::ShotBlocked, [&](const Event& e) { return here(e) && e.enemy; }) +
                                count(events, EventKind::ShotMissed, [&](const Event& e) { return here(e) && e.enemy; });
    assert(landed_on_enemy > 0);
    assert(count(events, EventKind::EnemyBeaten, [&](const Event& e) {
               return here(e) && e.ship == flagship.id;
           }) == 1);
    // Nothing of the fleet's was lost to a Scout.
    assert(count(events, EventKind::ShipLost) == 0 && count(events, EventKind::FleetLost) == 0);
}

// A shield projector on the flagship: knocked down to none, then back up.
void test_shields() {
    FleetShip player{1, "Flagship", Ship::new_game(), 0, std::nullopt};
    const GridPos port = layout_to_grid(starter_layout(), starter_layout().shield_ports.at(0));
    player.ship.inventory().add(ItemKind::ShieldProjector);
    assert(player.ship.place(ItemKind::ShieldProjector, port) == BuildResult::Ok);
    Rng rng{3};
    Engagement e = make_engagement(0, EnemyClass::Scout, rng);
    const std::vector<FleetShip*> ships{&player};
    const Progress progress;
    EventLog log;
    std::vector<Event> events;
    for (Tick now = 1; now < 120 * kTicksPerSecond; ++now) {
        player.ship.tick();
        combat_tick(e, ships, 1, progress, rng, now, log);
        for (const Event& ev : log.take())
            events.push_back(ev);
        if (count(events, EventKind::ShieldUp, [](const Event& ev) { return !ev.enemy; }) > 0)
            break;
    }
    const auto ours = [](const Event& ev) { return !ev.enemy && ev.ship == 1; };
    assert(count(events, EventKind::ShieldsDown, ours) > 0);
    assert(count(events, EventKind::ShieldUp, ours) > 0);
}

// With nothing to fight back with, a Gunship wins: the fleet is lost, as
// one event (not a ship lost as well).
void test_fleet_lost() {
    World world;
    const int id = world.get_fleet().flagship().id;
    // Something for its stray shots to destroy: a shield projector, which
    // its design goes after too.
    Ship& ship = world.flagship();
    ship.inventory().add(ItemKind::ShieldProjector);
    assert(ship.place(ItemKind::ShieldProjector, layout_to_grid(starter_layout(), starter_layout().shield_ports.at(0))) ==
           BuildResult::Ok);
    world.spawn_enemy(id, EnemyClass::Gunship);
    std::vector<Event> events;
    for (int i = 0; i < 30 * 60 * static_cast<int>(kTicksPerSecond) && !world.fleet_lost(); ++i)
        for (const Event& e : run(world, 1))
            events.push_back(e);
    assert(world.fleet_lost());
    assert(count(events, EventKind::FleetLost, [&](const Event& e) { return e.ship == id; }) == 1);
    assert(count(events, EventKind::ShipLost) == 0);
    const auto on_us = [](const Event& e) { return !e.enemy; };
    assert(count(events, EventKind::ShotHit, on_us) > 0);
    assert(count(events, EventKind::BuildingDestroyed, on_us) + count(events, EventKind::EngineOffline, on_us) > 0);
}

// Charging the engines reports them ready, once; jumping reports the jump.
void test_engines_and_jumps() {
    World world;
    FleetShip& flagship = world.get_fleet().flagship();
    run(world, 1);
    charge_thrusters(flagship.ship);
    std::vector<Event> events = run(world, 3);
    assert(count(events, EventKind::ThrustersCharged, [&](const Event& e) { return e.ship == flagship.id; }) == 1);
    const int to = world.get_map().at(flagship.sector).neighbors.front();
    assert(world.jump(flagship.id, to) == JumpResult::Ok);
    events = world.take_events();
    assert(count(events, EventKind::Jumped, [&](const Event& e) { return e.ship == flagship.id && e.sector == to; }) ==
           1);
    // The jump burned the fuel: no longer ready, so charging again reports it again.
    run(world, 1);
    charge_thrusters(flagship.ship);
    events = run(world, 1);
    assert(count(events, EventKind::ThrustersCharged) == 1);
}

// Cargo pods report landing, at the ship they land at.
void test_cargo() {
    World world;
    Fleet& fleet = world.get_fleet();
    Inventory& inv = world.flagship().inventory();
    inv.add(ItemKind::ShipStarterPack);
    inv.add(starter_layout().cost);
    int to = 0;
    assert(fleet.launch(fleet.flagship().id, starter_layout(), &to) == FleetResult::Ok);
    inv.add({{ItemKind::Gear, 10}, {ItemKind::CargoPod, 1}});
    assert(fleet.send(fleet.flagship().id, to, {{ItemKind::Gear, 10}}, world.get_tick()) == FleetResult::Ok);
    const std::vector<Event> events = run(world, kTransferTicks);
    assert(count(events, EventKind::CargoLanded, [&](const Event& e) { return e.ship == to; }) == 1);
}

// The last item a milestone wants, delivered by belt as the ship ticks,
// completes it: reported once.
void test_milestone() {
    World world;
    Ship& ship = world.flagship();
    const GridPos bridge = layout_to_grid(starter_layout(), starter_layout().bridge);
    const GridPos edge{bridge.x + kBridgeSize - 1, bridge.y};
    const GridPos beside{bridge.x + kBridgeSize, bridge.y};
    // All but one of what the first milestone wants, straight into the bridge.
    std::vector<ItemStack> wanted = all_milestones().front().deliver;
    const ItemKind last = wanted.back().kind;
    --wanted.back().count;
    for (const ItemStack& s : wanted)
        for (int i = 0; i < s.count; ++i)
            assert(ship.insert_item(edge, beside, s.kind));
    assert(world.get_fleet().get_progress().current_index() == 0);
    // A belt beside the bridge, pointing into it, with the last item on it.
    ship.inventory().add(ItemKind::TransportBelt);
    assert(ship.place(ItemKind::TransportBelt, beside, Direction::West) == BuildResult::Ok);
    assert(ship.insert_item(beside, {beside.x + 1, beside.y}, last));
    const std::vector<Event> events = run(world, 5 * kTicksPerSecond);
    assert(world.get_fleet().get_progress().current_index() == 1);
    assert(count(events, EventKind::MilestoneComplete) == 1);
}

// A log nobody takes keeps only the newest kCapacity events.
void test_log_cap() {
    EventLog log;
    for (int i = 0; i < static_cast<int>(EventLog::kCapacity) + 10; ++i)
        log.push({.kind = EventKind::Jumped, .ship = i});
    assert(log.peek().size() == EventLog::kCapacity);
    assert(log.peek().front().ship == 10);
    const std::vector<Event> taken = log.take();
    assert(taken.size() == EventLog::kCapacity && taken.back().ship == static_cast<int>(EventLog::kCapacity) + 9);
    assert(log.peek().empty());
}

}  // namespace

int main() {
    test_fight();
    test_shields();
    test_fleet_lost();
    test_engines_and_jumps();
    test_cargo();
    test_milestone();
    test_log_cap();
    std::puts("test_events: all passed");
    return 0;
}
