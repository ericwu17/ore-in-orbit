// test_fleet.cpp — headless tests for the fleet: launching a ship from a
// starter pack, sending cargo between ships in cargo pods, every ship
// ticking, and the milestone progress every ship shares.
#include "sim/building.h"
#include "sim/fleet.h"
#include "sim/milestones.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/world.h"

#include <cassert>
#include <cstdio>

namespace {

// The cell just east of the bridge's east side, on either kind of ship's
// hull, and the bridge cell next to it.
constexpr GridPos kBesideBridge{kStartBridgePos.x + kBridgeSize, kStartBridgePos.y};
constexpr GridPos kBridgeEdge{kStartBridgePos.x + kBridgeSize - 1, kStartBridgePos.y};

void run(World& world, Tick ticks) {
    for (Tick t = 0; t < ticks; ++t)
        world.advance_tick();
}

// Launches a second ship from the flagship; returns its id.
int launch_second(World& world) {
    Fleet& fleet = world.get_fleet();
    world.flagship().inventory().add(ItemKind::ShipStarterPack);
    int id = 0;
    assert(fleet.launch(fleet.flagship().id, &id) == FleetResult::Ok);
    return id;
}

void test_new_game() {
    const World world;
    const Fleet& fleet = world.get_fleet();
    assert(fleet.get_ships().size() == 1);
    assert(fleet.flagship().name == "Flagship" && fleet.flagship().sector == 0);
    assert(&world.flagship().get_bridge().get_progress() == &fleet.get_progress());
}

void test_launch() {
    World world;
    Fleet& fleet = world.get_fleet();
    const int flagship = fleet.flagship().id;
    assert(fleet.can_launch(flagship) == FleetResult::NoStarterPack);
    assert(fleet.launch(flagship) == FleetResult::NoStarterPack);
    assert(fleet.launch(99) == FleetResult::NoSuchShip);
    assert(fleet.get_ships().size() == 1);

    const int id = launch_second(world);
    assert(id != flagship);
    assert(world.flagship().inventory().count(ItemKind::ShipStarterPack) == 0);
    const FleetShip* launched = fleet.find(id);
    assert(launched && launched->name == "Ship " + std::to_string(id));
    assert(launched->sector == fleet.flagship().sector);

    // A small hull patch with the bridge in the middle, and some hull to
    // grow from.
    const Ship& ship = launched->ship;
    int hull = 0;
    for (int y = 0; y < kShipGridSize; ++y)
        for (int x = 0; x < kShipGridSize; ++x)
            hull += ship.has_hull({x, y}) ? 1 : 0;
    assert(hull == kLaunchedHullSide * kLaunchedHullSide);
    assert(ship.building_anchored_at(kStartBridgePos) == &ship.get_bridge());
    assert(ship.inventory().count(ItemKind::Hull) > 0);
    assert(ship.placement_order().size() == 1);

    // Ids never repeat.
    const int third = launch_second(world);
    assert(third != id && third != flagship);
}

void test_pods_needed() {
    assert(pods_needed(0) == 0);
    assert(pods_needed(1) == 1);
    assert(pods_needed(kPodCapacity) == 1);
    assert(pods_needed(kPodCapacity + 1) == 2);
    assert(pods_needed(2 * kPodCapacity) == 2);
}

void test_send() {
    World world;
    Fleet& fleet = world.get_fleet();
    const int from = fleet.flagship().id;
    const int to = launch_second(world);
    Inventory& source = world.flagship().inventory();
    Inventory& target = fleet.find(to)->ship.inventory();
    source = Inventory{};
    source.add({{ItemKind::Gear, 60}, {ItemKind::Circuit, 10}});

    assert(fleet.send(from, from, {{ItemKind::Gear, 1}}, 0) == FleetResult::SameShip);
    assert(fleet.send(from, 99, {{ItemKind::Gear, 1}}, 0) == FleetResult::NoSuchShip);
    assert(fleet.send(from, to, {}, 0) == FleetResult::NothingToSend);
    assert(fleet.send(from, to, {{ItemKind::Gear, 0}}, 0) == FleetResult::NothingToSend);
    assert(fleet.send(from, to, {{ItemKind::Gear, 61}}, 0) == FleetResult::NotEnoughItems);
    assert(fleet.send(from, to, {{ItemKind::Gear, 10}}, 0) == FleetResult::NotEnoughPods);

    // 60 gears and 10 circuits: two pods' worth.
    source.add(ItemKind::CargoPod, 1);
    assert(fleet.can_send(from, to, {{ItemKind::Gear, 60}, {ItemKind::Circuit, 10}}) == FleetResult::NotEnoughPods);
    source.add(ItemKind::CargoPod, 2);
    const int hull_before = target.count(ItemKind::Hull);
    assert(fleet.send(from, to, {{ItemKind::Gear, 60}, {ItemKind::Circuit, 10}}, world.get_tick()) ==
           FleetResult::Ok);
    assert(source.count(ItemKind::Gear) == 0 && source.count(ItemKind::Circuit) == 0);
    assert(source.count(ItemKind::CargoPod) == 1);
    assert(fleet.in_flight().size() == 1);

    // It lands kTransferTicks later, not before.
    run(world, kTransferTicks - 1);
    assert(target.count(ItemKind::Gear) == 0);
    run(world, 1);
    assert(target.count(ItemKind::Gear) == 60 && target.count(ItemKind::Circuit) == 10);
    assert(target.count(ItemKind::Hull) == hull_before);
    assert(fleet.in_flight().empty());
}

// Ships trade only within a sector.
void test_other_sector() {
    World world;
    Fleet& fleet = world.get_fleet();
    const int from = fleet.flagship().id;
    const int to = launch_second(world);
    world.flagship().inventory().add({{ItemKind::Gear, 5}, {ItemKind::CargoPod, 1}});
    fleet.find(to)->sector = 3;
    assert(fleet.send(from, to, {{ItemKind::Gear, 5}}, 0) == FleetResult::OtherSector);
}

// Cargo goes into the inventory, but (like hand-crafting) doesn't count
// toward the milestone; a delivery by belt to any ship does, and unlocks
// recipes on every ship.
void test_shared_progress() {
    World world;
    Fleet& fleet = world.get_fleet();
    const int from = fleet.flagship().id;
    const int to = launch_second(world);
    Ship& second = fleet.find(to)->ship;
    const Progress& progress = fleet.get_progress();
    const Milestone& first = all_milestones().front();

    world.flagship().inventory().add({{ItemKind::IronPlate, 10}, {ItemKind::CargoPod, 1}});
    assert(fleet.send(from, to, {{ItemKind::IronPlate, 10}}, world.get_tick()) == FleetResult::Ok);
    run(world, kTransferTicks);
    assert(second.inventory().count(ItemKind::IronPlate) == 10);
    assert(progress.delivered_count(ItemKind::IronPlate) == 0);

    // Deliver the whole first milestone to the second ship's bridge.
    for (const ItemStack& s : first.deliver)
        for (int i = 0; i < s.count; ++i)
            assert(second.insert_item(kBridgeEdge, kBesideBridge, s.kind));
    assert(progress.current_index() == 1);
    // Steel is unlocked by it, on the flagship too.
    assert(world.flagship().get_bridge().get_progress().unlocked(ItemKind::Steel));
    world.flagship().inventory().add({{ItemKind::IronPlate, 3}, {ItemKind::Carbon, 1}});
    const GridPos furnace{kBesideBridge.x + 1, kBesideBridge.y + 2};
    assert(world.flagship().place(ItemKind::SolarFurnace, furnace) == BuildResult::Ok);
    assert(world.flagship().set_recipe(furnace, recipe_for(ItemKind::Steel)));
}

// Every ship ticks, not just the flagship.
void test_every_ship_ticks() {
    World world;
    Fleet& fleet = world.get_fleet();
    const int id = launch_second(world);
    Bridge& bridge = fleet.find(id)->ship.get_bridge();
    bridge.get_inventory().add(ItemKind::IronPlate, 2);
    const Recipe& gear = *recipe_for(ItemKind::Gear);
    assert(bridge.queue_craft(gear));
    run(world, gear.ticks);
    assert(bridge.get_inventory().count(ItemKind::Gear) == 1);
}

void test_rename() {
    World world;
    Fleet& fleet = world.get_fleet();
    assert(fleet.rename(fleet.flagship().id, "Ark"));
    assert(fleet.flagship().name == "Ark");
    assert(!fleet.rename(99, "Nobody"));
}

}  // namespace

int main() {
    test_new_game();
    test_launch();
    test_pods_needed();
    test_send();
    test_other_sector();
    test_shared_progress();
    test_every_ship_ticks();
    test_rename();
    std::puts("test_fleet: all passed");
    return 0;
}
