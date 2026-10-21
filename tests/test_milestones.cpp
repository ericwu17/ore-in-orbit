// test_milestones.cpp — headless tests for the milestones: deliveries to the
// bridge count toward the current one (hand-crafts and refunds don't),
// completing one moves on and unlocks its recipes, locked recipes can't be
// hand-crafted or picked for a machine, and every milestone can be reached
// with what the ones before it unlock.
#include "sim/building.h"
#include "sim/milestones.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/world.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>

namespace {

// The cell just east of the bridge's east side, on the starting hull.
constexpr GridPos kBesideBridge{kStartBridgePos.x + kBridgeSize, kStartBridgePos.y};
constexpr GridPos kBridgeEdge{kStartBridgePos.x + kBridgeSize - 1, kStartBridgePos.y};

void test_table() {
    const std::vector<Milestone>& milestones = all_milestones();
    assert(!milestones.empty());
    std::array<int, kItemKindCount> unlocked_by{};
    for (const Milestone& m : milestones) {
        assert(!m.deliver.empty() && !m.unlocks.empty());
        for (const ItemStack& s : m.deliver)
            assert(s.count > 0);
        // Each item is unlocked once, and only items with a recipe.
        for (const ItemKind kind : m.unlocks) {
            assert(recipe_for(kind));
            assert(++unlocked_by[static_cast<std::size_t>(kind)] == 1);
        }
    }
    // The first line needs nothing unlocked.
    for (const ItemKind kind : {ItemKind::Hull, ItemKind::TransportBelt, ItemKind::AsteroidCollector,
                                ItemKind::SolarFurnace, ItemKind::FabricatorMk1, ItemKind::IronPlate,
                                ItemKind::CopperPlate, ItemKind::Gear, ItemKind::Circuit})
        assert(!milestone_unlocking(kind));
    assert(milestone_unlocking(ItemKind::Refinery) == 1);
}

// Whether `kind` can be had with only what's unlocked before milestone
// `before`: in a new game's inventory (which breaks the cycles: collectors
// are made from plates smelted from what they gather), gathered by an
// unlocked collector, or made by an unlocked recipe (run by hand or by an
// unlocked machine) from such items.
bool reachable(ItemKind kind, std::size_t before, int depth = 0) {
    assert(depth < 32);
    static const Ship start = Ship::new_game();
    if (start.inventory().count(kind) > 0)
        return true;
    auto unlocked = [&](ItemKind k) {
        const std::optional<std::size_t> at = milestone_unlocking(k);
        return !at || *at < before;
    };
    if (item_category(kind) == ItemCategory::RawResource) {
        for (const ItemKind collector : {ItemKind::AsteroidCollector, ItemKind::TractorArray}) {
            const std::vector<ItemKind>& targets = collector_spec(collector).targets;
            if (unlocked(collector) && reachable(collector, before, depth + 1) &&
                std::find(targets.begin(), targets.end(), kind) != targets.end())
                return true;
        }
        return false;
    }
    const Recipe* r = recipe_for(kind);
    if (!r || !unlocked(kind))
        return false;
    bool runnable = r->hand_craftable;
    for (int i = 0; i < kItemKindCount && !runnable; ++i) {
        const ItemKind machine = static_cast<ItemKind>(i);
        runnable = machine_crafts(machine, r->category) && unlocked(machine) &&
                   reachable(machine, before, depth + 1);
    }
    if (!runnable)
        return false;
    for (const ItemStack& s : r->inputs)
        if (!reachable(s.kind, before, depth + 1))
            return false;
    return true;
}

// Every milestone asks only for what the ones before it make possible, so
// the game can't get stuck; and everything is reachable once they're done.
void test_no_dead_ends() {
    const std::vector<Milestone>& milestones = all_milestones();
    for (std::size_t i = 0; i < milestones.size(); ++i)
        for (const ItemStack& s : milestones[i].deliver) {
            if (!reachable(s.kind, i))
                std::printf("milestone %zu can't get %s\n", i + 1, item_name(s.kind));
            assert(reachable(s.kind, i));
        }
    for (int i = 0; i < kItemKindCount; ++i) {
        const ItemKind kind = static_cast<ItemKind>(i);
        if (!reachable(kind, milestones.size()))
            std::printf("unreachable: %s\n", item_name(kind));
        assert(reachable(kind, milestones.size()));
    }
}

// Deliveries count, capped at what's needed; the last one completes the
// milestone, which resets the counts for the next.
void test_progress() {
    Progress progress;
    const Milestone& first = all_milestones().front();
    assert(progress.current_milestone() == &first);
    assert(!progress.unlocked(ItemKind::Steel));
    assert(!progress.on_delivered(ItemKind::Gear));  // not asked for
    assert(progress.delivered_count(ItemKind::Gear) == 0);

    const ItemStack& a = first.deliver[0];
    const ItemStack& b = first.deliver[1];
    for (int i = 0; i < a.count + 5; ++i)
        assert(!progress.on_delivered(a.kind));
    assert(progress.delivered_count(a.kind) == a.count);
    for (int i = 1; i < b.count; ++i)
        assert(!progress.on_delivered(b.kind));
    assert(progress.on_delivered(b.kind));

    assert(progress.current_index() == 1);
    assert(progress.delivered_count(a.kind) == 0 && progress.delivered_count(b.kind) == 0);
    for (const ItemKind kind : first.unlocks)
        assert(progress.unlocked(kind));
    assert(!progress.unlocked(ItemKind::Refinery));

    while (progress.complete_current()) {
    }
    assert(progress.all_complete());
    assert(progress.current_index() == all_milestones().size());
    assert(!progress.on_delivered(ItemKind::Processor));
    for (int i = 0; i < kItemKindCount; ++i)
        assert(progress.unlocked(static_cast<ItemKind>(i)));
}

// On the ship: what a neighbor puts into the bridge counts, hand-crafts
// don't; locked recipes are refused by hand and in machines.
void test_on_ship() {
    World world;
    Ship& ship = world.flagship();
    Bridge& bridge = ship.get_bridge();
    const Progress& progress = bridge.get_progress();

    assert(ship.insert_item(kBridgeEdge, kBesideBridge, ItemKind::IronPlate));
    assert(progress.delivered_count(ItemKind::IronPlate) == 1);
    const Recipe& iron = *recipe_for(ItemKind::IronPlate);
    ship.inventory().add(ItemKind::IronOre, 1);
    assert(bridge.queue_craft(iron));
    for (Tick t = 0; t < iron.ticks; ++t)
        world.advance_tick();
    assert(progress.delivered_count(ItemKind::IronPlate) == 1);

    // Steel waits for the first milestone, by hand and in a furnace.
    const Recipe& steel = *recipe_for(ItemKind::Steel);
    const Recipe& frame = *recipe_for(ItemKind::StructuralFrame);
    ship.inventory().add({{ItemKind::IronPlate, 10}, {ItemKind::Carbon, 2}, {ItemKind::Steel, 2},
                          {ItemKind::Gear, 2}});
    assert(!bridge.can_hand_craft(frame) && !bridge.queue_craft(frame));
    const GridPos furnace{kBesideBridge.x + 1, kBesideBridge.y + 2};
    assert(ship.place(ItemKind::SolarFurnace, furnace) == BuildResult::Ok);
    assert(!ship.set_recipe(furnace, &steel));
    assert(ship.set_recipe(furnace, &iron));

    // Deliver the rest of the first milestone.
    for (const ItemStack& s : all_milestones().front().deliver)
        while (progress.current_index() == 0 && progress.delivered_count(s.kind) < s.count)
            assert(ship.insert_item(kBridgeEdge, kBesideBridge, s.kind));
    assert(progress.current_index() == 1);
    assert(ship.set_recipe(furnace, &steel));
    // The frame waits for the second.
    assert(!bridge.can_hand_craft(frame));
}

}  // namespace

int main() {
    test_table();
    test_no_dead_ends();
    test_progress();
    test_on_ship();
    std::puts("test_milestones: all passed");
    return 0;
}
