// test_fleet.cpp — headless tests for the fleet: building a ship in a
// layout, from a starter pack and the layout's cost, sending cargo between
// ships in cargo pods, every ship ticking, and the milestone progress every
// ship shares.
#include "sim/building.h"
#include "sim/fleet.h"
#include "sim/milestones.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"
#include "sim/world.h"

#include <cassert>
#include <cstdio>

namespace {

// The starter layout's bridge on the grid.
GridPos bridge_pos() {
    return layout_to_grid(starter_layout(), starter_layout().bridge);
}
// The cell just east of the bridge's east side, on a starter layout's hull,
// and the bridge cell next to it.
const GridPos kBesideBridge{bridge_pos().x + kBridgeSize, bridge_pos().y};
const GridPos kBridgeEdge{bridge_pos().x + kBridgeSize - 1, bridge_pos().y};

// Puts what building a ship in `layout` takes into `inv`: a starter pack and
// the layout's cost.
void stock_for(Inventory& inv, const ShipLayout& layout) {
    inv.add(ItemKind::ShipStarterPack);
    inv.add(layout.cost);
}

void run(World& world, Tick ticks) {
    for (Tick t = 0; t < ticks; ++t)
        world.advance_tick();
}

// Launches a second ship, in the starter layout, from the flagship; returns
// its id.
int launch_second(World& world) {
    Fleet& fleet = world.get_fleet();
    stock_for(world.flagship().inventory(), starter_layout());
    int id = 0;
    assert(fleet.launch(fleet.flagship().id, starter_layout(), &id) == FleetResult::Ok);
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
    const ShipLayout& skiff = starter_layout();
    Inventory& inv = world.flagship().inventory();
    inv = Inventory{};
    assert(fleet.can_launch(flagship, skiff) == FleetResult::NoStarterPack);
    assert(fleet.launch(flagship, skiff) == FleetResult::NoStarterPack);
    assert(fleet.launch(99, skiff) == FleetResult::NoSuchShip);
    // A starter pack isn't enough: the layout has a cost too.
    inv.add(ItemKind::ShipStarterPack);
    assert(fleet.launch(flagship, skiff) == FleetResult::NotEnoughForLayout);
    assert(fleet.get_ships().size() == 1);

    inv.add(skiff.cost);
    inv.add(ItemKind::Gear, 7);  // left alone
    int id = 0;
    assert(fleet.launch(flagship, skiff, &id) == FleetResult::Ok);
    assert(id != flagship);
    // The pack and the cost are used up, and nothing else.
    assert(inv.total() == 7 && inv.count(ItemKind::Gear) == 7);
    const FleetShip* launched = fleet.find(id);
    assert(launched && launched->name == "Ship " + std::to_string(id));
    assert(launched->sector == fleet.flagship().sector);

    // The layout's hull, bridge and engines, and an empty inventory.
    const Ship& ship = launched->ship;
    assert(ship.get_layout() == &skiff);
    assert(ship.hull_count() == layout_hull_count(skiff));
    assert(ship.building_anchored_at(bridge_pos()) == &ship.get_bridge());
    assert(ship.thrusters().size() == skiff.engines.size());
    assert(ship.placement_order().size() == 1 + skiff.engines.size());
    assert(ship.inventory().total() == 0);

    // Ids never repeat.
    const int third = launch_second(world);
    assert(third != id && third != flagship);
}

// Bigger layouts unlock with the milestones.
void test_layout_unlocks() {
    World world;
    Fleet& fleet = world.get_fleet();
    const int flagship = fleet.flagship().id;
    const ShipLayout& hauler = *find_layout("hauler");
    assert(hauler.milestone);
    stock_for(world.flagship().inventory(), hauler);
    assert(fleet.can_launch(flagship, starter_layout()) != FleetResult::LayoutLocked);
    assert(fleet.launch(flagship, hauler) == FleetResult::LayoutLocked);
    while (fleet.get_progress().current_index() <= *hauler.milestone)
        assert(fleet.get_progress().complete_current());
    int id = 0;
    assert(fleet.launch(flagship, hauler, &id) == FleetResult::Ok);
    assert(fleet.find(id)->ship.get_layout() == &hauler);
    assert(fleet.find(id)->ship.thrusters().size() == hauler.engines.size());
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
    const int total_before = target.total();
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
    assert(target.total() == total_before + 70);
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
    test_layout_unlocks();
    test_pods_needed();
    test_send();
    test_other_sector();
    test_shared_progress();
    test_every_ship_ticks();
    test_rename();
    std::puts("test_fleet: all passed");
    return 0;
}
