// test_production.cpp — headless tests for the production buildings: the
// collectors' rates, their internal buffers (and stalling when full), what
// they're aimed at, and refunding a buffer when the building comes off.
#include "sim/building.h"
#include "sim/ship.h"
#include "sim/world.h"

#include <cassert>
#include <cstdio>
#include <memory>

namespace {

// On the starting hull, with room for a 2x2 building, diagonal from the
// bridge's bottom-right corner (not beside it: it would hand its output to
// the bridge).
constexpr GridPos kOnHull{kStartBridgePos.x + kBridgeSize, kStartBridgePos.y + kBridgeSize};

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
    assert(!ship.set_collector_target(kStartBridgePos, ItemKind::Carbon));

    const int items_before = ship.inventory().total() - ship.inventory().count(ItemKind::AsteroidCollector);
    const int collectors = ship.inventory().count(ItemKind::AsteroidCollector);
    assert(ship.remove_building(kOnHull) == BuildResult::Ok);
    assert(ship.inventory().count(ItemKind::AsteroidCollector) == collectors + 1);
    assert(ship.inventory().total() - ship.inventory().count(ItemKind::AsteroidCollector) == items_before + 3);
}

}  // namespace

int main() {
    test_asteroid_rate_and_buffer();
    test_targets();
    test_on_ship();
    std::puts("test_production: all passed");
    return 0;
}
