// test_production.cpp — headless tests for the production buildings: the
// collectors' rates, their internal buffers (and stalling when full), the
// chunk mix they catch, and refunding a buffer when the building comes off.
#include "sim/building.h"
#include "sim/ship.h"
#include "sim/world.h"

#include <cassert>
#include <cstdio>
#include <memory>

namespace {

constexpr GridPos kCenter{kShipGridSize / 2, kShipGridSize / 2};
// On the starting hull, with room for a 2x2 building east of the bridge.
constexpr GridPos kOnHull{kCenter.x + 1, kCenter.y};

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

    // One chunk per interval, not before.
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

// The chunk mix matches the weights exactly over one full round, and every
// kind turns up (exotic chunks are the only source of iridium).
void test_chunk_mix() {
    for (const ItemKind kind : {ItemKind::AsteroidCollector, ItemKind::TractorArray}) {
        const auto c = make_collector(kind);
        const CollectorSpec& spec = c->get_spec();
        int round = 0;
        for (const int w : spec.chunk_weights)
            round += w;
        Inventory caught;
        for (int i = 0; i < round; ++i) {
            tick(*c, spec.interval);
            const std::vector<ItemStack> held = c->items().stacks();
            assert(held.size() == 1 && held[0].count == 1);
            caught.add(held[0].kind);
            assert(c->items().take(held[0].kind));
        }
        for (int i = 0; i < kChunkKindCount; ++i) {
            const auto chunk = static_cast<ItemKind>(static_cast<int>(ItemKind::MetallicChunk) + i);
            assert(item_category(chunk) == ItemCategory::AsteroidChunk);
            assert(caught.count(chunk) == spec.chunk_weights[static_cast<std::size_t>(i)]);
            assert(caught.count(chunk) > 0);
        }
    }
    // The tractor array is the faster collector.
    assert(make_collector(ItemKind::TractorArray)->rate_per_second() >
           make_collector(ItemKind::AsteroidCollector)->rate_per_second());
}

void test_hydrogen() {
    const auto c = make_collector(ItemKind::HydrogenCollector);
    const CollectorSpec& spec = c->get_spec();
    assert(spec.fluid == FluidKind::Hydrogen);
    assert(c->rate_per_second() == 10.0);
    tick(*c, spec.interval);
    assert(c->fluid_amount() == spec.fluid_amount);
    assert(c->items().total() == 0);
    tick(*c, spec.interval * 100);
    assert(c->full() && c->fluid_amount() == c->capacity());
    assert(c->contents().empty());  // fluid isn't refunded as items
}

// Placed on the ship, collectors run with the world; removing one refunds
// what it caught along with the building.
void test_on_ship() {
    World world;
    Ship& ship = world.get_ship();
    assert(ship.place(ItemKind::AsteroidCollector, kOnHull) == BuildResult::Ok);
    const auto* c = dynamic_cast<const Collector*>(ship.building_at(kOnHull));
    assert(c);
    for (Tick t = 0; t < c->get_spec().interval * 3; ++t)
        world.advance_tick();
    assert(c->stored() == 3);

    const int chunks_before = ship.inventory().total() - ship.inventory().count(ItemKind::AsteroidCollector);
    const int collectors = ship.inventory().count(ItemKind::AsteroidCollector);
    assert(ship.remove_building(kOnHull) == BuildResult::Ok);
    assert(ship.inventory().count(ItemKind::AsteroidCollector) == collectors + 1);
    assert(ship.inventory().total() - ship.inventory().count(ItemKind::AsteroidCollector) == chunks_before + 3);
}

}  // namespace

int main() {
    test_asteroid_rate_and_buffer();
    test_chunk_mix();
    test_hydrogen();
    test_on_ship();
    std::puts("test_production: all passed");
    return 0;
}
