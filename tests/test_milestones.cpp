// test_milestones.cpp — headless tests for the milestones: what machines and
// hand-crafting produce counts toward the current one (moving items about,
// into the bridge or out of it, doesn't), completing one moves on and
// unlocks its recipes, locked recipes can't be hand-crafted or picked for a machine, and
// every milestone can be reached with what the ones before it unlock, and
// leaving home never waits on what home might run out of.
#include "sim/building.h"
#include "sim/milestones.h"
#include "sim/recipes.h"
#include "sim/sector.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"
#include "sim/world.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>

namespace {

// The cell just east of the bridge's east side, on the starting hull.
const GridPos kBridgePos = layout_to_grid(starter_layout(), starter_layout().bridge);
const GridPos kBesideBridge{kBridgePos.x + kBridgeSize, kBridgePos.y};
const GridPos kBridgeEdge{kBridgePos.x + kBridgeSize - 1, kBridgePos.y};

void test_table() {
    const std::vector<Milestone>& milestones = all_milestones();
    assert(!milestones.empty());
    std::array<int, kItemKindCount> unlocked_by{};
    for (const Milestone& m : milestones) {
        assert(!m.produce.empty() && !m.unlocks.empty());
        for (const ItemStack& s : m.produce)
            assert(s.count > 0);
        // Each item is unlocked once, and only items with a recipe.
        for (const ItemKind kind : m.unlocks) {
            assert(recipe_for(kind));
            assert(++unlocked_by[static_cast<std::size_t>(kind)] == 1);
        }
    }
    // The first line needs nothing unlocked.
    for (const ItemKind kind : {ItemKind::TransportBelt, ItemKind::AsteroidCollector,
                                ItemKind::SolarFurnace, ItemKind::FabricatorMk1, ItemKind::IronPlate,
                                ItemKind::CopperPlate, ItemKind::Gear, ItemKind::Circuit})
        assert(!milestone_unlocking(kind));
    assert(milestone_unlocking(ItemKind::Refinery) == 1);
    // Names are unique, so tables can name a milestone (milestone_index).
    for (std::size_t i = 0; i < milestones.size(); ++i)
        assert(milestone_index(milestones[i].name) == i);
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
        for (const ItemStack& s : milestones[i].produce) {
            if (!reachable(s.kind, i))
                std::printf("milestone %zu can't get %s\n", i + 1, item_name(s.kind));
            assert(reachable(s.kind, i));
        }
    for (int i = 0; i < kItemKindCount; ++i) {
        const ItemKind kind = static_cast<ItemKind>(i);
        if (is_layout_part(kind) || is_weapon(kind))
            continue;  // comes with a ship's layout, or is found, never made
        if (!reachable(kind, milestones.size()))
            std::printf("unreachable: %s\n", item_name(kind));
        assert(reachable(kind, milestones.size()));
    }
}

// What's produced counts, past what's needed too; the last one completes
// the milestone, which resets the counts for the next.
void test_progress() {
    Progress progress;
    const Milestone& first = all_milestones().front();
    assert(progress.current_milestone() == &first);
    assert(!progress.unlocked(ItemKind::Steel));
    assert(!progress.on_produced(ItemKind::Gear));  // not asked for
    assert(progress.produced_count(ItemKind::Gear) == 0);

    const ItemStack& a = first.produce[0];
    const ItemStack& b = first.produce[1];
    for (int i = 0; i < a.count + 5; ++i)
        assert(!progress.on_produced(a.kind));
    assert(progress.produced_count(a.kind) == a.count + 5);
    // A craft making several counts them all.
    assert(!progress.on_produced({{b.kind, b.count - 2}, {ItemKind::Gear, 1}}));
    assert(progress.produced_count(b.kind) == b.count - 2);
    assert(!progress.on_produced(b.kind));
    assert(progress.on_produced(b.kind));

    assert(progress.current_index() == 1);
    assert(progress.produced_count(a.kind) == 0 && progress.produced_count(b.kind) == 0);
    for (const ItemKind kind : first.unlocks)
        assert(progress.unlocked(kind));
    assert(!progress.unlocked(ItemKind::Refinery));

    while (progress.complete_current()) {
    }
    assert(progress.all_complete());
    assert(progress.current_index() == all_milestones().size());
    assert(!progress.on_produced(ItemKind::Processor));
    // Everything but weapons and what only finding one unlocks (see
    // test_combat).
    const auto found_only = [](ItemKind kind) { return is_weapon(kind) || weapon_unlocking(kind).has_value(); };
    for (int i = 0; i < kItemKindCount; ++i)
        assert(progress.unlocked(static_cast<ItemKind>(i)) == !found_only(static_cast<ItemKind>(i)));
    for (int i = 0; i < kItemKindCount; ++i)
        if (is_weapon(static_cast<ItemKind>(i)))
            assert(progress.add_found(static_cast<ItemKind>(i)));
    for (int i = 0; i < kItemKindCount; ++i)
        assert(progress.unlocked(static_cast<ItemKind>(i)));
}

// Moving items about counts for nothing: neither items going round from the
// bridge, through an unloader and belts, back into it, nor ones put straight
// into it.
void test_moving_doesnt_count() {
    World world;
    Ship& ship = world.flagship();
    const Progress& progress = ship.get_bridge().get_progress();
    assert(ship.insert_item(kBridgeEdge, kBesideBridge, ItemKind::IronPlate));
    assert(progress.produced_count(ItemKind::IronPlate) == 0);

    ship.inventory().add(ItemKind::Unloader, 1);
    const GridPos unloader{kBesideBridge.x, kBesideBridge.y + 1};
    assert(ship.place(ItemKind::Unloader, unloader, Direction::East) == BuildResult::Ok);
    assert(ship.set_filter(unloader, ItemKind::IronPlate));
    assert(ship.place(ItemKind::TransportBelt, {unloader.x + 1, unloader.y}, Direction::North) == BuildResult::Ok);
    assert(ship.place(ItemKind::TransportBelt, {unloader.x + 1, kBesideBridge.y}, Direction::West) ==
           BuildResult::Ok);
    assert(ship.place(ItemKind::TransportBelt, kBesideBridge, Direction::West) == BuildResult::Ok);

    const int plates = ship.inventory().count(ItemKind::IronPlate);
    bool went_round = false;
    for (Tick t = 0; t < 10 * kTicksPerSecond; ++t) {
        world.advance_tick();
        went_round = went_round || ship.inventory().count(ItemKind::IronPlate) < plates;
        assert(progress.produced_count(ItemKind::IronPlate) == 0);
    }
    assert(went_round);

    assert(ship.remove_building(unloader) == BuildResult::Ok);
    for (Tick t = 0; t < 5 * kTicksPerSecond; ++t)
        world.advance_tick();
    assert(progress.produced_count(ItemKind::IronPlate) == 0);
    assert(ship.inventory().count(ItemKind::IronPlate) == plates);
}

// On the ship: hand-crafts count when they finish, and so do a machine's
// crafts, whether or not its products go anywhere; locked recipes are
// refused by hand and in machines.
void test_on_ship() {
    World world;
    Ship& ship = world.flagship();
    Bridge& bridge = ship.get_bridge();
    const Progress& progress = bridge.get_progress();

    const Recipe& iron = *recipe_for(ItemKind::IronPlate);
    ship.inventory().add(ItemKind::IronOre, 1);
    assert(bridge.queue_craft(iron));
    assert(progress.produced_count(ItemKind::IronPlate) == 0);  // not yet made
    for (Tick t = 0; t < iron.ticks; ++t)
        world.advance_tick();
    assert(progress.produced_count(ItemKind::IronPlate) == 1);

    // Steel waits for the first milestone, by hand and in a furnace.
    const Recipe& steel = *recipe_for(ItemKind::Steel);
    const Recipe& frame = *recipe_for(ItemKind::StructuralFrame);
    ship.inventory().add({{ItemKind::IronPlate, 10}, {ItemKind::Carbon, 2}, {ItemKind::Steel, 2},
                          {ItemKind::Gear, 2}});
    assert(!bridge.can_hand_craft(frame) && !bridge.queue_craft(frame));
    // A furnace on its own, with nowhere to put out its plates.
    const GridPos furnace{kBesideBridge.x + 1, kBesideBridge.y + 2};
    assert(ship.place(ItemKind::SolarFurnace, furnace) == BuildResult::Ok);
    assert(!ship.set_recipe(furnace, &steel));
    assert(ship.set_recipe(furnace, &iron));
    const GridPos fed_from{furnace.x - 1, furnace.y};
    const int plates = ship.inventory().count(ItemKind::IronPlate);
    for (int i = 0; i < 2; ++i)
        assert(ship.insert_item(furnace, fed_from, ItemKind::IronOre));
    for (Tick t = 0; t < 3 * iron.ticks; ++t)
        world.advance_tick();
    assert(progress.produced_count(ItemKind::IronPlate) == 3);
    assert(ship.inventory().count(ItemKind::IronPlate) == plates);  // still in the furnace

    // Make the rest of the first milestone.
    for (const ItemStack& s : all_milestones().front().produce)
        while (progress.current_index() == 0 && progress.produced_count(s.kind) < s.count)
            bridge.get_progress().on_produced(s.kind);
    assert(progress.current_index() == 1);
    assert(ship.set_recipe(furnace, &steel));
    // The frame waits for the second.
    assert(!bridge.can_hand_craft(frame));
}

// What `count` of `kind` take in raw resources, by kRawResources index.
std::array<double, kRawResourceCount> raw_cost(ItemKind kind, double count) {
    std::array<double, kRawResourceCount> cost{};
    if (const std::optional<int> r = raw_resource_index(kind)) {
        cost[static_cast<std::size_t>(*r)] = count;
        return cost;
    }
    const Recipe* recipe = recipe_for(kind);
    assert(recipe);
    const double runs = count / recipe->outputs.front().count;
    for (const ItemStack& in : recipe->inputs) {
        const std::array<double, kRawResourceCount> part = raw_cost(in.kind, runs * in.count);
        for (std::size_t i = 0; i < cost.size(); ++i)
            cost[i] += part[i];
    }
    return cost;
}

// Home's resources run out, so leaving it can't wait on anything they might
// not cover: fuel cells take only what home has, are unlocked before
// Escape velocity (which a player who spent home's silica couldn't finish),
// and the milestones up to them and a first jump take well under half of
// what home holds.
void test_travel_from_home() {
    const std::optional<std::size_t> fuel_at = milestone_unlocking(ItemKind::FuelCell);
    assert(fuel_at && *fuel_at < milestone_index("Escape velocity"));
    const auto fuel = raw_cost(ItemKind::FuelCell, 1.0);
    for (int r = 0; r < kRawResourceCount; ++r)
        assert(fuel[static_cast<std::size_t>(r)] == 0.0 || kRawResources[r] == ItemKind::Carbon);

    std::array<double, kRawResourceCount> cost = raw_cost(
        ItemKind::FuelCell, static_cast<double>(kThrusterCharge * starter_layout().engines.size()));
    for (std::size_t m = 0; m <= *fuel_at; ++m)
        for (const ItemStack& s : all_milestones()[m].produce) {
            const std::array<double, kRawResourceCount> part = raw_cost(s.kind, s.count);
            for (std::size_t i = 0; i < cost.size(); ++i)
                cost[i] += part[i];
        }
    for (std::size_t i = 0; i < cost.size(); ++i)
        assert(cost[i] * 2.0 <= kHomeReserves[i]);
}

}  // namespace

int main() {
    test_table();
    test_no_dead_ends();
    test_progress();
    test_moving_doesnt_count();
    test_on_ship();
    test_travel_from_home();
    std::puts("test_milestones: all passed");
    return 0;
}
