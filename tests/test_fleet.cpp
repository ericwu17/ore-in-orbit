// test_fleet.cpp — headless tests for the fleet: building a ship in a
// layout, from a starter pack and the layout's cost, sending cargo between
// ships in cargo pods, every ship ticking, and the delivery progress every
// ship shares.
#include "sim/building.h"
#include "sim/fleet.h"
#include "sim/deliveries.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"
#include "sim/world.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <optional>
#include <vector>

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

// Bigger layouts unlock with the techs.
void test_layout_unlocks() {
    World world;
    Fleet& fleet = world.get_fleet();
    const int flagship = fleet.flagship().id;
    const ShipLayout& hauler = *find_layout("hauler");
    assert(hauler.tech);
    stock_for(world.flagship().inventory(), hauler);
    assert(fleet.can_launch(flagship, starter_layout()) != FleetResult::LayoutLocked);
    assert(fleet.launch(flagship, hauler) == FleetResult::LayoutLocked);
    for (std::size_t t = 0; t <= *hauler.tech; ++t)
        fleet.get_progress().grant(t);
    assert(fleet.get_progress().bought(*hauler.tech));
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

template <typename Fits>
GridPos place_somewhere(Ship& ship, ItemKind kind, Fits fits);
GridPos place_somewhere(Ship& ship, ItemKind kind);

// Cargo goes into the inventory, and counts toward no delivery; what any
// ship's providers deliver does, and its points buy techs for every ship.
void test_shared_progress() {
    World world;
    Fleet& fleet = world.get_fleet();
    const int from = fleet.flagship().id;
    const int to = launch_second(world);
    Ship& second = fleet.find(to)->ship;
    const Progress& progress = fleet.get_progress();
    const std::size_t first = 0;
    const Delivery& delivery = all_deliveries()[first];

    world.flagship().inventory().add({{ItemKind::IronPlate, 10}, {ItemKind::CargoPod, 1}});
    assert(fleet.send(from, to, {{ItemKind::IronPlate, 10}}, world.get_tick()) == FleetResult::Ok);
    run(world, kTransferTicks);
    assert(second.inventory().count(ItemKind::IronPlate) == 10);
    assert(progress.delivered(first, ItemKind::IronPlate) == 0);

    // The second ship's provider makes the whole first delivery, in the
    // sector that takes it.
    fleet.find(to)->sector = world.get_map().delivery_sector(first);
    const GridPos at = place_somewhere(second, ItemKind::Provider);
    auto& provider = dynamic_cast<Provider&>(*second.building_anchored_at(at));
    for (const ItemStack& s : delivery.wants)
        while (progress.delivered(first, s.kind) < s.count) {
            if (provider.accepts(s.kind))
                provider.receive(s.kind);
            run(world, 1);
        }
    assert(progress.done(first));
    // It paid out, for every ship; the tech its points buy unlocks steel
    // on the flagship too.
    Progress& flagship_progress = world.flagship().get_bridge().get_progress();
    assert(flagship_progress.points() == delivery_points(first));
    flagship_progress.add_points(all_techs()[tech_index("Steelworks")].cost);
    assert(flagship_progress.buy(tech_index("Steelworks")));
    assert(progress.unlocked(ItemKind::Steel));
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

// Providers ----------------------------------------------------------------

// Runs `world` up to and through its next dispatch.
void run_to_dispatch(World& world) {
    run(world, kDispatchInterval - world.get_tick() % kDispatchInterval);
}

// A world of `n` ships in the home sector, every bridge emptied: the
// flagship first, then the ships launched from it. Returns their ids, oldest
// first.
std::vector<int> fleet_of(World& world, int n) {
    std::vector<int> ids{world.get_fleet().flagship().id};
    for (int i = 1; i < n; ++i)
        ids.push_back(launch_second(world));
    for (const int id : ids)
        world.get_fleet().find(id)->ship.inventory() = Inventory{};
    return ids;
}

Inventory& inv(World& world, int id) {
    return world.get_fleet().find(id)->ship.inventory();
}
Bridge& bridge(World& world, int id) {
    return world.get_fleet().find(id)->ship.get_bridge();
}

// A provider tops up a request in its sector, by pod, and the cargo on its
// way counts toward the request, so it isn't sent twice.
void test_provider_fills_request() {
    World world;
    Fleet& fleet = world.get_fleet();
    const std::vector<int> ids = fleet_of(world, 2);
    const int provider = ids[0], requester = ids[1];
    inv(world, provider).add({{ItemKind::Gear, 30}, {ItemKind::CargoPod, 2}});
    bridge(world, requester).set_request(ItemKind::Gear, 20);

    // Not a provider: nothing moves.
    run_to_dispatch(world);
    assert(fleet.in_flight().empty());

    assert(fleet.set_provides(provider, true));
    assert(!fleet.set_provides(99, true));
    run_to_dispatch(world);
    assert(fleet.in_flight().size() == 1);
    const Transfer& t = fleet.in_flight()[0];
    assert(t.automatic && t.from == provider && t.to == requester);
    assert(t.items.size() == 1 && t.items[0].kind == ItemKind::Gear && t.items[0].count == 20);
    assert(inv(world, provider).count(ItemKind::Gear) == 10);
    assert(inv(world, provider).count(ItemKind::CargoPod) == 1);
    assert(fleet.in_flight_to(requester, ItemKind::Gear) == 20);

    // In flight, it counts: the next dispatch sends nothing more.
    run_to_dispatch(world);
    assert(fleet.in_flight().size() == 1);
    run(world, kTransferTicks);
    assert(inv(world, requester).count(ItemKind::Gear) == 20);
    run_to_dispatch(world);
    assert(fleet.in_flight().empty() && inv(world, provider).count(ItemKind::Gear) == 10);

    // Used up, the request is topped up again.
    assert(inv(world, requester).take(ItemKind::Gear, 5));
    run_to_dispatch(world);
    assert(fleet.in_flight_to(requester, ItemKind::Gear) == 5);
}

// Only ships in the same sector are topped up, and never the provider from
// itself.
void test_provider_sector() {
    World world;
    Fleet& fleet = world.get_fleet();
    const std::vector<int> ids = fleet_of(world, 2);
    const int provider = ids[0], requester = ids[1];
    inv(world, provider).add({{ItemKind::Gear, 30}, {ItemKind::CargoPod, 2}});
    assert(fleet.set_provides(provider, true));
    bridge(world, provider).set_request(ItemKind::Steel, 10);  // none to be had
    bridge(world, requester).set_request(ItemKind::Gear, 20);
    fleet.find(requester)->sector = 3;
    run_to_dispatch(world);
    assert(fleet.in_flight().empty());
    fleet.find(requester)->sector = 0;
    run_to_dispatch(world);
    assert(fleet.in_flight().size() == 1);
}

// A provider keeps what it requests itself, and gives only the rest.
void test_provider_keeps_its_own() {
    World world;
    Fleet& fleet = world.get_fleet();
    const std::vector<int> ids = fleet_of(world, 2);
    const int provider = ids[0], requester = ids[1];
    inv(world, provider).add({{ItemKind::Gear, 100}, {ItemKind::CargoPod, 2}});
    bridge(world, provider).set_request(ItemKind::Gear, 100);
    assert(fleet.set_provides(provider, true));
    bridge(world, requester).set_request(ItemKind::Gear, 20);
    run_to_dispatch(world);
    assert(fleet.in_flight().empty());

    // Both providers, asking for the same: nothing bounces between them.
    assert(fleet.set_provides(requester, true));
    inv(world, requester).add(ItemKind::CargoPod, 2);
    bridge(world, provider).set_request(ItemKind::Gear, 90);
    // 10 to spare, short of the 20 asked for: a part-filled pod, which
    // waits for more before it goes.
    run_to_dispatch(world);
    assert(fleet.in_flight().empty());
    run(world, kPartialPodWait);
    assert(fleet.in_flight().size() == 1 && fleet.in_flight()[0].from == provider);
    run(world, kTransferTicks + 4 * kDispatchInterval);
    assert(fleet.in_flight().empty());
    assert(inv(world, provider).count(ItemKind::Gear) == 90);
    assert(inv(world, requester).count(ItemKind::Gear) == 10);
}

// The provider with the most to spare gives first; ties go to the oldest,
// and what one can't give, the next one does.
void test_most_surplus_first() {
    World world;
    Fleet& fleet = world.get_fleet();
    const std::vector<int> ids = fleet_of(world, 3);
    const int a = ids[0], requester = ids[1], c = ids[2];
    for (const int p : {a, c}) {
        inv(world, p).add(ItemKind::CargoPod, 5);
        assert(fleet.set_provides(p, true));
    }
    inv(world, a).add(ItemKind::Gear, 10);
    inv(world, c).add(ItemKind::Gear, 40);
    bridge(world, requester).set_request(ItemKind::Gear, 30);
    run_to_dispatch(world);
    assert(fleet.in_flight().size() == 1 && fleet.in_flight()[0].from == c);

    // A tie: the oldest.
    bridge(world, requester).set_request(ItemKind::Circuit, 5);
    inv(world, a).add(ItemKind::Circuit, 8);
    inv(world, c).add(ItemKind::Circuit, 8);
    run_to_dispatch(world);
    assert(fleet.in_flight().size() == 2 && fleet.in_flight()[1].from == a);

    // More than either has: the bigger stock first, then the rest.
    bridge(world, requester).set_request(ItemKind::Steel, 15);
    inv(world, a).add(ItemKind::Steel, 6);
    inv(world, c).add(ItemKind::Steel, 9);
    run_to_dispatch(world);
    assert(fleet.in_flight_to(requester, ItemKind::Steel) == 15);
    assert(inv(world, a).count(ItemKind::Steel) == 0 && inv(world, c).count(ItemKind::Steel) == 0);
}

// Automatic sends use up pods: none, none sent; whole pods go at once, a
// part-filled one only once it finishes the job or has waited.
void test_provider_pods() {
    World world;
    Fleet& fleet = world.get_fleet();
    const std::vector<int> ids = fleet_of(world, 2);
    const int provider = ids[0], requester = ids[1];
    assert(fleet.set_provides(provider, true));
    inv(world, provider).add(ItemKind::Gear, 70);
    bridge(world, requester).set_request(ItemKind::Gear, 120);
    run_to_dispatch(world);
    assert(fleet.in_flight().empty() && fleet.short_of_pods(provider));

    // One pod: one pod's worth, now; the 20 left wait to fill a pod.
    inv(world, provider).add(ItemKind::CargoPod, 1);
    run_to_dispatch(world);
    assert(fleet.short_of_pods(provider));  // for the 20 more
    assert(fleet.in_flight().size() == 1 && fleet.in_flight_to(requester, ItemKind::Gear) == kPodCapacity);
    inv(world, provider).add(ItemKind::CargoPod, 1);
    run_to_dispatch(world);
    assert(fleet.in_flight().size() == 1);
    // Having waited, they go anyway.
    run(world, kPartialPodWait);
    assert(inv(world, requester).count(ItemKind::Gear) + fleet.in_flight_to(requester, ItemKind::Gear) == 70);
    assert(inv(world, provider).count(ItemKind::Gear) == 0 && inv(world, provider).count(ItemKind::CargoPod) == 0);
}

// Cargo pods asked for come out of the same stock as the pods that carry
// them: a provider keeps back what carrying them takes.
void test_provider_sends_pods() {
    World world;
    Fleet& fleet = world.get_fleet();
    const std::vector<int> ids = fleet_of(world, 2);
    const int provider = ids[0], requester = ids[1];
    assert(fleet.set_provides(provider, true));
    inv(world, provider).add(ItemKind::CargoPod, 20);
    bridge(world, requester).set_request(ItemKind::CargoPod, 20);
    // 19 and the one that carries them: short of the 20 asked for, so a
    // part-filled pod that waits.
    run_to_dispatch(world);
    assert(fleet.in_flight().empty() && !fleet.short_of_pods(provider));
    run(world, kPartialPodWait);
    assert(fleet.in_flight_to(requester, ItemKind::CargoPod) == 19);
    assert(inv(world, provider).count(ItemKind::CargoPod) == 0);
    run_to_dispatch(world);
    assert(!fleet.short_of_pods(provider));
}

// A provider without the pods for what it has to send says so at once,
// even while it holds back a part-filled pod.
void test_short_of_pods_while_waiting() {
    World world;
    Fleet& fleet = world.get_fleet();
    const std::vector<int> ids = fleet_of(world, 2);
    const int provider = ids[0], requester = ids[1];
    assert(fleet.set_provides(provider, true));
    inv(world, provider).add(ItemKind::Gear, 30);
    bridge(world, requester).set_request(ItemKind::Gear, 120);
    run_to_dispatch(world);
    assert(fleet.in_flight().empty() && fleet.short_of_pods(provider));
    inv(world, provider).add(ItemKind::CargoPod, 1);
    run_to_dispatch(world);
    assert(fleet.in_flight().empty() && !fleet.short_of_pods(provider));
}

// A provider without pods to carry its stock leaves the request to one with
// them, rather than holding on to it.
void test_podless_provider_steps_aside() {
    World world;
    Fleet& fleet = world.get_fleet();
    const std::vector<int> ids = fleet_of(world, 3);
    const int a = ids[0], requester = ids[1], c = ids[2];
    for (const int p : {a, c})
        assert(fleet.set_provides(p, true));
    inv(world, a).add(ItemKind::Gear, 1000);
    inv(world, c).add({{ItemKind::Gear, 100}, {ItemKind::CargoPod, 10}});
    bridge(world, requester).set_request(ItemKind::Gear, 100);
    run_to_dispatch(world);
    assert(fleet.in_flight().size() == 1 && fleet.in_flight()[0].from == c);
    assert(fleet.in_flight_to(requester, ItemKind::Gear) == 100);
    assert(!fleet.short_of_pods(a));  // c had it covered
}

// Pods asked for ahead of a great many items: the most that fits is found
// at once, and the pods go with as many items as the last one carries.
void test_large_request_after_pods() {
    World world;
    Fleet& fleet = world.get_fleet();
    const std::vector<int> ids = fleet_of(world, 2);
    const int provider = ids[0], requester = ids[1];
    assert(fleet.set_provides(provider, true));
    inv(world, provider).add({{ItemKind::CargoPod, 31}, {ItemKind::Gear, kMaxRequest}});
    bridge(world, requester).set_request(ItemKind::CargoPod, 30);
    bridge(world, requester).set_request(ItemKind::Gear, kMaxRequest);
    const auto start = std::chrono::steady_clock::now();
    run_to_dispatch(world);
    const auto took = std::chrono::steady_clock::now() - start;
    assert(took < std::chrono::milliseconds(100));
    // 30 pods and 20 gears fill the one pod left to carry them.
    assert(fleet.in_flight_to(requester, ItemKind::CargoPod) == 30);
    assert(fleet.in_flight_to(requester, ItemKind::Gear) == 20);
    assert(inv(world, provider).count(ItemKind::CargoPod) == 0);
    assert(fleet.short_of_pods(provider));
}

// A part-filled pod that has waited, but had no pod to go in, goes as soon
// as one turns up.
void test_waited_pod_goes_with_first_pod() {
    World world;
    Fleet& fleet = world.get_fleet();
    const std::vector<int> ids = fleet_of(world, 2);
    const int provider = ids[0], requester = ids[1];
    assert(fleet.set_provides(provider, true));
    inv(world, provider).add(ItemKind::Gear, 30);
    bridge(world, requester).set_request(ItemKind::Gear, 120);
    run(world, kPartialPodWait + kDispatchInterval);
    assert(fleet.in_flight().empty());
    inv(world, provider).add(ItemKind::CargoPod, 1);
    run_to_dispatch(world);
    assert(fleet.in_flight_to(requester, ItemKind::Gear) == 30);
}

// --- Providers and requesters ---------------------------------------------

// Places a `kind` (stocked for it) on the first free hull cell of `ship`
// where `fits` also holds, scanning row by row; returns where.
template <typename Fits>
GridPos place_somewhere(Ship& ship, ItemKind kind, Fits fits) {
    ship.inventory().add(kind, 1);
    for (int y = 0; y < ship.size(); ++y)
        for (int x = 0; x < ship.size(); ++x)
            if (ship.can_place(kind, {x, y}) == BuildResult::Ok && fits(GridPos{x, y})) {
                assert(ship.place(kind, {x, y}) == BuildResult::Ok);
                return {x, y};
            }
    assert(false && "no room on the ship");
    return {};
}

GridPos place_somewhere(Ship& ship, ItemKind kind) {
    return place_somewhere(ship, kind, [](GridPos) { return true; });
}

// Whether nothing stands next to `p` but, perhaps, on `except`: a requester
// there puts its items out only where the test means it to.
bool alone(const Ship& ship, GridPos p, std::optional<Direction> except = std::nullopt) {
    for (const Direction d : {Direction::North, Direction::East, Direction::South, Direction::West})
        if (d != except && ship.building_at(neighbor(p, d)))
            return false;
    return true;
}

// A requester with a cargo hold just east of it, to put its items into;
// returns the requester's cell (the hold is one cell east).
GridPos place_requester_into_hold(Ship& ship) {
    ship.inventory().add(ItemKind::CargoHold, 1);
    const GridPos at = place_somewhere(ship, ItemKind::Requester, [&](GridPos p) {
        return alone(ship, p, Direction::East) && ship.can_place(ItemKind::CargoHold, {p.x + 1, p.y}) == BuildResult::Ok;
    });
    assert(ship.place(ItemKind::CargoHold, {at.x + 1, at.y}) == BuildResult::Ok);
    return at;
}

Provider& provider_at(Ship& ship, GridPos p) {
    return dynamic_cast<Provider&>(*ship.building_anchored_at(p));
}

const Requester& requester_at(const Ship& ship, GridPos p) {
    return dynamic_cast<const Requester&>(*ship.building_anchored_at(p));
}

int held_in_hold(const Ship& ship, GridPos requester) {
    return dynamic_cast<const CargoHold&>(*ship.building_at({requester.x + 1, requester.y})).get_stored().total();
}

// Fills the provider up with `kind`, as belts feeding it flat out would.
void top_up(Provider& provider, ItemKind kind) {
    while (provider.accepts(kind))
        provider.receive(kind);
}

// Runs `ticks`, keeping the provider at `p` on `ship` full of `kind`.
void run_fed(World& world, Ship& ship, GridPos p, ItemKind kind, Tick ticks) {
    for (Tick t = 0; t < ticks; ++t) {
        top_up(provider_at(ship, p), kind);
        world.advance_tick();
    }
}

// A requester pulls its item from a provider on another ship in the sector,
// at an express belt's rate, and puts it out to its neighbors.
void test_link_between_ships() {
    World world;
    Fleet& fleet = world.get_fleet();
    Ship& from = world.flagship();
    Ship& to = fleet.find(launch_second(world))->ship;
    const GridPos provider = place_somewhere(from, ItemKind::Provider);
    const GridPos requester = place_requester_into_hold(to);

    // No filter: nothing asks for anything, so it takes nothing.
    Provider& p = provider_at(from, provider);
    run_fed(world, from, provider, ItemKind::Gear, kTicksPerSecond);
    assert(p.get_stored().total() == 0 && !p.accepts(ItemKind::Circuit) && held_in_hold(to, requester) == 0);

    // Set to another item: it takes that, and still no gears.
    assert(to.set_filter(requester, ItemKind::Circuit));
    run_fed(world, from, provider, ItemKind::Gear, kTicksPerSecond);
    assert(p.get_stored().total() == 0 && p.accepts(ItemKind::Circuit) && held_in_hold(to, requester) == 0);

    assert(to.set_filter(requester, ItemKind::Gear));
    run_fed(world, from, provider, ItemKind::Gear, kTicksPerSecond);  // warm up
    const int before = held_in_hold(to, requester);
    assert(before > 0);
    constexpr Tick kSeconds = 5;
    run_fed(world, from, provider, ItemKind::Gear, kSeconds * kTicksPerSecond);
    const int per_second = static_cast<int>(kTicksPerSecond / kLinkTicks);
    assert(per_second == 12);
    const int moved = held_in_hold(to, requester) - before;
    assert(std::abs(moved - per_second * static_cast<int>(kSeconds)) <= 1);
}

// Only within a sector.
void test_link_other_sector() {
    World world;
    Fleet& fleet = world.get_fleet();
    Ship& from = world.flagship();
    const int id = launch_second(world);
    Ship& to = fleet.find(id)->ship;
    const GridPos provider = place_somewhere(from, ItemKind::Provider);
    const GridPos requester = place_requester_into_hold(to);
    assert(to.set_filter(requester, ItemKind::Gear));
    fleet.find(id)->sector = 3;
    run_fed(world, from, provider, ItemKind::Gear, kTicksPerSecond);
    assert(held_in_hold(to, requester) == 0);
    fleet.find(id)->sector = fleet.flagship().sector;
    run_fed(world, from, provider, ItemKind::Gear, kTicksPerSecond);
    assert(held_in_hold(to, requester) > 0);
}

// A provider serves requesters on its own ship too.
void test_link_same_ship() {
    World world;
    Ship& ship = world.flagship();
    const GridPos provider = place_somewhere(ship, ItemKind::Provider);
    const GridPos requester = place_requester_into_hold(ship);
    assert(ship.set_filter(requester, ItemKind::Gear));
    run_fed(world, ship, provider, ItemKind::Gear, kTicksPerSecond);
    assert(held_in_hold(ship, requester) > 0);
}

// With nowhere to put its items, a requester pulls kRequesterBuffer and stops,
// and then the provider takes no more for it: they wait on the belts. A
// new filter refunds what the requester held to the bridge.
void test_requester_buffer() {
    World world;
    Fleet& fleet = world.get_fleet();
    Ship& from = world.flagship();
    Ship& to = fleet.find(launch_second(world))->ship;
    const GridPos provider = place_somewhere(from, ItemKind::Provider);
    const GridPos requester =
        place_somewhere(to, ItemKind::Requester, [&](GridPos p) { return alone(to, p); });
    assert(to.set_filter(requester, ItemKind::Gear));
    run_fed(world, from, provider, ItemKind::Gear, 10 * kTicksPerSecond);
    assert(requester_at(to, requester).get_buffer().count(ItemKind::Gear) == kRequesterBuffer);
    assert(provider_at(from, provider).get_stored().total() == 0);
    assert(!provider_at(from, provider).accepts(ItemKind::Gear));

    const int gears = to.inventory().count(ItemKind::Gear);
    assert(to.set_filter(requester, ItemKind::Circuit));
    assert(requester_at(to, requester).get_buffer().total() == 0);
    assert(to.inventory().count(ItemKind::Gear) == gears + kRequesterBuffer);
}

// Two requesters on one provider share what it sends, which is no more
// than its own rate.
void test_requesters_share() {
    World world;
    Fleet& fleet = world.get_fleet();
    Ship& from = world.flagship();
    Ship& to = fleet.find(launch_second(world))->ship;
    const GridPos provider = place_somewhere(from, ItemKind::Provider);
    const GridPos a = place_requester_into_hold(to);
    const GridPos b = place_requester_into_hold(to);
    assert(to.set_filter(a, ItemKind::Gear) && to.set_filter(b, ItemKind::Gear));
    constexpr Tick kSeconds = 5;
    run_fed(world, from, provider, ItemKind::Gear, kSeconds * kTicksPerSecond);
    const int got_a = held_in_hold(to, a);
    const int got_b = held_in_hold(to, b);
    const int rate = static_cast<int>(kTicksPerSecond / kLinkTicks) * static_cast<int>(kSeconds);
    assert(got_a + got_b <= rate + 1 && got_a + got_b >= rate - 2);
    assert(got_a >= rate / 4 && got_b >= rate / 4);
}

// However many requesters share one provider, each gets as many as the
// others, give or take one: the one that has waited longest goes first.
// (Taking turns by the tick instead starved all but one or two of them when
// there were a multiple of 5, kLinkTicks, of them.)
void test_requesters_share_evenly() {
    for (const int n : {2, 3, 4, 5, 6, 7, 10}) {
        World world;
        Fleet& fleet = world.get_fleet();
        Ship& from = world.flagship();
        Ship& to = fleet.find(launch_second(world))->ship;
        const GridPos provider = place_somewhere(from, ItemKind::Provider);
        std::vector<GridPos> requesters;
        for (int i = 0; i < n; ++i) {
            requesters.push_back(
                place_somewhere(to, ItemKind::Requester, [&](GridPos p) { return alone(to, p); }));
            assert(to.set_filter(requesters.back(), ItemKind::Gear));
        }
        // Short enough that none fills up (and stops asking).
        run_fed(world, from, provider, ItemKind::Gear, kTicksPerSecond);
        int least = kRequesterBuffer;
        int most = 0;
        for (const GridPos r : requesters) {
            const int got = requester_at(to, r).get_buffer().total();
            least = std::min(least, got);
            most = std::max(most, got);
        }
        assert(most < kRequesterBuffer);
        assert(most - least <= 1);
    }
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
    test_provider_fills_request();
    test_provider_sector();
    test_provider_keeps_its_own();
    test_most_surplus_first();
    test_provider_pods();
    test_provider_sends_pods();
    test_short_of_pods_while_waiting();
    test_podless_provider_steps_aside();
    test_large_request_after_pods();
    test_waited_pod_goes_with_first_pod();
    test_link_between_ships();
    test_link_other_sector();
    test_link_same_ship();
    test_requester_buffer();
    test_requesters_share();
    test_requesters_share_evenly();
    std::puts("test_fleet: all passed");
    return 0;
}
