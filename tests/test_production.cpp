// test_production.cpp — headless tests for the production buildings: the
// collectors' rates, their internal buffers (and stalling when full), what
// they're aimed at, and refunding a buffer when the building comes off; and
// the bridge's item requests, which machines and collectors can be set to
// run only on.
#include "sim/building.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"
#include "sim/world.h"

#include <cassert>
#include <cstdio>
#include <memory>

namespace {

// On the starting hull, with room for a 2x2 building, diagonal from the
// bridge's bottom-right corner (not beside it: it would hand its output to
// the bridge).
const GridPos kBridgePos = layout_to_grid(starter_layout(), starter_layout().bridge);
const GridPos kOnHull{kBridgePos.x + kBridgeSize, kBridgePos.y + kBridgeSize};

std::unique_ptr<Collector> make_collector(ItemKind kind) {
    std::unique_ptr<Building> b = make_building(kind, Direction::North);
    auto* c = dynamic_cast<Collector*>(b.get());
    assert(c);
    b.release();
    return std::unique_ptr<Collector>(c);
}

void tick(Building& b, Tick n) {
    for (Tick t = 0; t < n; ++t)
        b.tick();
}

void test_asteroid_rate_and_buffer() {
    const auto c = make_collector(ItemKind::AsteroidCollector);
    const CollectorSpec& spec = c->get_spec();
    assert(spec.interval == 2 * kTicksPerSecond);
    assert(c->rate_per_second() == 0.5);
    assert(c->stored() == 0 && !c->full());

    // One catch per interval, not before.
    tick(*c, spec.interval - 1);
    assert(c->stored() == 0);
    assert(c->catch_progress() > 0.9f);
    tick(*c, 1);
    assert(c->stored() == 1);
    assert(c->catch_progress() == 0.0f);

    // Fills to capacity, then stalls.
    tick(*c, spec.interval * static_cast<Tick>(c->capacity() + 5));
    assert(c->full());
    assert(c->stored() == c->capacity());
    // Making room lets it carry on.
    const std::vector<ItemStack> held = c->items().stacks();
    assert(c->items().take(held.front().kind));
    assert(!c->full());
    tick(*c, spec.interval);
    assert(c->full());
}

// A collector catches only what it's aimed at: iron ore when new, then
// whatever it's switched to, with what it already caught left in its
// buffer. Only the tractor array gathers iridium.
void test_targets() {
    const auto c = make_collector(ItemKind::AsteroidCollector);
    const CollectorSpec& spec = c->get_spec();
    assert(c->get_target() == ItemKind::IronOre);
    tick(*c, spec.interval * 2);
    assert(c->items().count(ItemKind::IronOre) == 2);

    c->set_target(ItemKind::Silica);
    tick(*c, spec.interval * 3);
    assert(c->items().count(ItemKind::IronOre) == 2);
    assert(c->items().count(ItemKind::Silica) == 3);
    assert(c->stored() == 5);

    for (const ItemKind target : spec.targets)
        assert(item_category(target) == ItemCategory::RawResource);
    assert(!c->can_target(ItemKind::IridiumOre));
    assert(!c->can_target(ItemKind::IronPlate));
    assert(make_collector(ItemKind::TractorArray)->can_target(ItemKind::IridiumOre));

    // The tractor array is the faster collector.
    assert(make_collector(ItemKind::TractorArray)->rate_per_second() >
           make_collector(ItemKind::AsteroidCollector)->rate_per_second());
}

// Placed on the ship, collectors run with the world; with no neighbor to
// take its catch, removing one refunds it along with the building.
void test_on_ship() {
    World world;
    Ship& ship = world.flagship();
    assert(ship.place(ItemKind::AsteroidCollector, kOnHull) == BuildResult::Ok);
    const auto* c = dynamic_cast<const Collector*>(ship.building_at(kOnHull));
    assert(c);
    for (Tick t = 0; t < c->get_spec().interval * 3; ++t)
        world.advance_tick();
    assert(c->stored() == 3);

    // Aiming it goes through the ship; what it can't gather is refused.
    assert(ship.set_collector_target(kOnHull, ItemKind::Carbon));
    assert(c->get_target() == ItemKind::Carbon);
    assert(!ship.set_collector_target(kOnHull, ItemKind::IridiumOre));
    assert(c->get_target() == ItemKind::Carbon);
    assert(!ship.set_collector_target(kBridgePos, ItemKind::Carbon));

    const int items_before = ship.inventory().total() - ship.inventory().count(ItemKind::AsteroidCollector);
    const int collectors = ship.inventory().count(ItemKind::AsteroidCollector);
    assert(ship.remove_building(kOnHull) == BuildResult::Ok);
    assert(ship.inventory().count(ItemKind::AsteroidCollector) == collectors + 1);
    assert(ship.inventory().total() - ship.inventory().count(ItemKind::AsteroidCollector) == items_before + 3);
}

// The bridge's requests: in the order asked for, one per item, unmet while
// the inventory holds fewer.
void test_requests() {
    Bridge bridge;
    assert(bridge.get_requests().empty() && !bridge.request_for(ItemKind::Gear));
    assert(!bridge.request_unmet(ItemKind::Gear));
    bridge.set_request(ItemKind::Gear, 3);
    bridge.set_request(ItemKind::Circuit, 0);
    assert(bridge.get_requests().size() == 2 && bridge.get_requests()[0].kind == ItemKind::Gear);
    assert(bridge.request_for(ItemKind::Gear) == 3 && bridge.request_for(ItemKind::Circuit) == 0);
    assert(bridge.request_unmet(ItemKind::Gear) && !bridge.request_unmet(ItemKind::Circuit));
    bridge.get_inventory().add(ItemKind::Gear, 3);
    assert(!bridge.request_unmet(ItemKind::Gear));
    // Changing a count keeps its place.
    bridge.set_request(ItemKind::Gear, 4);
    assert(bridge.get_requests().size() == 2 && bridge.get_requests()[0].count == 4);
    assert(bridge.request_unmet(ItemKind::Gear));
    bridge.remove_request(ItemKind::Gear);
    assert(bridge.get_requests().size() == 1 && !bridge.request_for(ItemKind::Gear));
    assert(!bridge.request_unmet(ItemKind::Gear));
}

// A collector set to run only on request gathers only while the bridge
// holds fewer of its target than it asks for, with none asked for, never;
// its buffer still goes out. New ones always run.
void test_collector_on_request() {
    World world;
    Ship& ship = world.flagship();
    assert(ship.place(ItemKind::AsteroidCollector, kOnHull) == BuildResult::Ok);
    const auto* c = dynamic_cast<const Collector*>(ship.building_at(kOnHull));
    const Tick interval = c->get_spec().interval;
    assert(!c->get_only_when_requested());
    const auto run = [&](Tick ticks) {
        for (Tick t = 0; t < ticks; ++t)
            world.advance_tick();
    };
    // Off: it runs whatever the bridge asks for.
    ship.get_bridge().set_request(ItemKind::IronOre, 0);
    run(interval);
    assert(c->stored() == 1 && !c->on_standby());

    assert(ship.set_only_when_requested(kOnHull, true));
    assert(!ship.set_only_when_requested(kBridgePos, true));
    // Its request is met (0 asked for): it gathers nothing.
    run(interval * 2);
    assert(c->stored() == 1 && c->on_standby());
    // None asked for: still nothing.
    ship.get_bridge().remove_request(ItemKind::IronOre);
    run(interval * 2);
    assert(c->stored() == 1 && c->on_standby());
    // Asked for more than the bridge has: it gathers.
    const int have = ship.inventory().count(ItemKind::IronOre);
    ship.get_bridge().set_request(ItemKind::IronOre, have + 2);
    run(interval * 2);
    assert(c->stored() == 3 && !c->on_standby());
    // A belt carries its catch into the bridge, until it has what it asked
    // for; then it stops, its buffer emptied.
    const GridPos belt{kOnHull.x - 1, kOnHull.y};
    ship.inventory().add(ItemKind::TransportBelt, 1);
    assert(ship.place(ItemKind::TransportBelt, belt, Direction::North) == BuildResult::Ok);
    ship.inventory().add(ItemKind::IronOre, 2);
    run(interval * 10);
    assert(c->on_standby() && c->stored() == 0);
}

// A machine set to run only on request finishes the craft under way once
// the request is met, then starts no other, though it still takes
// ingredients.
void test_machine_on_request() {
    World world;
    Ship& ship = world.flagship();
    assert(ship.place(ItemKind::SolarFurnace, kOnHull) == BuildResult::Ok);
    const Recipe* plate = nullptr;
    for (const Recipe* r : machine_recipes(ItemKind::SolarFurnace))
        if (r->outputs.front().kind == ItemKind::IronPlate)
            plate = r;
    assert(plate && ship.set_recipe(kOnHull, plate));
    const auto* m = dynamic_cast<const Machine*>(ship.building_at(kOnHull));
    assert(!m->get_only_when_requested() && product_of(*m) == ItemKind::IronPlate);
    assert(ship.set_only_when_requested(kOnHull, true));
    const GridPos west{kOnHull.x - 1, kOnHull.y};
    Bridge& bridge = ship.get_bridge();
    bridge.set_request(ItemKind::IronPlate, ship.inventory().count(ItemKind::IronPlate) + 5);

    assert(ship.insert_item(kOnHull, west, ItemKind::IronOre));
    world.advance_tick();
    assert(m->crafting());
    // Met part way through: the craft finishes, and no other starts.
    bridge.set_request(ItemKind::IronPlate, 0);
    assert(ship.insert_item(kOnHull, west, ItemKind::IronOre));
    for (Tick t = 0; t < plate->ticks * 3; ++t)
        world.advance_tick();
    assert(m->on_standby() && !m->crafting());
    assert(m->get_output().count(ItemKind::IronPlate) == 1);
    assert(m->get_input().count(ItemKind::IronOre) == 1);
    assert(ship.insert_item(kOnHull, west, ItemKind::IronOre));
    assert(m->get_input().count(ItemKind::IronOre) == 2);
    // Asked for again: it carries on.
    bridge.set_request(ItemKind::IronPlate, ship.inventory().count(ItemKind::IronPlate) + 5);
    for (Tick t = 0; t < plate->ticks * 3; ++t)
        world.advance_tick();
    assert(!m->on_standby() && m->get_output().count(ItemKind::IronPlate) == 3);
}

}  // namespace

int main() {
    test_asteroid_rate_and_buffer();
    test_targets();
    test_on_ship();
    test_requests();
    test_collector_on_request();
    test_machine_on_request();
    std::puts("test_production: all passed");
    return 0;
}
