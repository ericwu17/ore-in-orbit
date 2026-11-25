// test_deliveries.cpp — headless tests for the deliveries: the table (each
// delivery reachable with what the ones before it unlock, asking for more,
// and further up the chain, than those before it), progress through them
// (open ones, branches, completing one unlocking its recipes), providers
// making them in the sector that takes them (and nowhere else), where the
// map puts them (safe sectors, the first next to home, branches apart), and
// travel from the very start: fuel cells from a fabricator, and providers.
#include "sim/building.h"
#include "sim/deliveries.h"
#include "sim/recipes.h"
#include "sim/sector.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"
#include "sim/world.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <deque>
#include <set>

namespace {

void test_table() {
    const std::vector<Delivery>& deliveries = all_deliveries();
    assert(deliveries.size() >= 6);
    std::array<int, kItemKindCount> unlocked_by{};
    for (std::size_t d = 0; d < deliveries.size(); ++d) {
        const Delivery& delivery = deliveries[d];
        assert(!delivery.wants.empty() && !delivery.unlocks.empty());
        for (const ItemStack& s : delivery.wants)
            assert(s.count > 0 && item_category(s.kind) != ItemCategory::Placeable);
        // Each item is unlocked once, and only items with a recipe.
        for (const ItemKind kind : delivery.unlocks) {
            assert(recipe_for(kind));
            assert(++unlocked_by[static_cast<std::size_t>(kind)] == 1);
        }
        // Each comes after deliveries listed before it.
        for (const std::size_t before : delivery_prerequisites(d))
            assert(before < d);
        // Names are unique, so tables can name a delivery (delivery_index).
        assert(delivery_index(delivery.name) == d);
    }
    // Exactly one to start with, and the first line needs nothing unlocked.
    std::size_t roots = 0;
    for (std::size_t d = 0; d < deliveries.size(); ++d)
        roots += delivery_prerequisites(d).empty() ? 1u : 0u;
    assert(roots == 1 && delivery_tier(0) == 1);
    for (const ItemKind kind : {ItemKind::TransportBelt, ItemKind::AsteroidCollector, ItemKind::SolarFurnace,
                                ItemKind::FabricatorMk1, ItemKind::IronPlate, ItemKind::CopperPlate, ItemKind::Gear,
                                ItemKind::Circuit, ItemKind::FuelCell, ItemKind::Provider})
        assert(!delivery_unlocking(kind));
    // Branches: some tier has more than one delivery, for the player to
    // pick between.
    std::array<int, 16> per_tier{};
    for (std::size_t d = 0; d < deliveries.size(); ++d)
        ++per_tier[static_cast<std::size_t>(delivery_tier(d))];
    assert(std::any_of(per_tier.begin(), per_tier.end(), [](int n) { return n >= 2; }));
    // Every bigger ship layout comes with a delivery.
    for (const ShipLayout& layout : all_layouts())
        assert(&layout == &starter_layout() ? !layout.delivery : layout.delivery && *layout.delivery < deliveries.size());
}

// Every delivery that has to be complete before `d` can be open (its
// prerequisites, and theirs).
std::set<std::size_t> needed_before(std::size_t d) {
    std::set<std::size_t> needed;
    for (const std::size_t p : delivery_prerequisites(d)) {
        needed.insert(p);
        const std::set<std::size_t> more = needed_before(p);
        needed.insert(more.begin(), more.end());
    }
    return needed;
}

// Whether `kind` can be had with only what's unlocked by the deliveries in
// `done`: in a new game's inventory (which breaks the cycles: collectors
// are made from plates smelted from what they gather), gathered by an
// unlocked collector, or made by an unlocked recipe (run by hand or by an
// unlocked machine) from such items.
bool reachable(ItemKind kind, const std::set<std::size_t>& done, int depth = 0) {
    assert(depth < 32);
    static const Ship start = Ship::new_game();
    if (start.inventory().count(kind) > 0)
        return true;
    auto unlocked = [&](ItemKind k) {
        const std::optional<std::size_t> at = delivery_unlocking(k);
        return !at || done.contains(*at);
    };
    if (item_category(kind) == ItemCategory::RawResource) {
        for (const ItemKind collector : {ItemKind::AsteroidCollector, ItemKind::TractorArray}) {
            const std::vector<ItemKind>& targets = collector_spec(collector).targets;
            if (unlocked(collector) && reachable(collector, done, depth + 1) &&
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
        runnable = machine_crafts(machine, r->category) && unlocked(machine) && reachable(machine, done, depth + 1);
    }
    if (!runnable)
        return false;
    for (const ItemStack& s : r->inputs)
        if (!reachable(s.kind, done, depth + 1))
            return false;
    return true;
}

// Every delivery asks only for what the ones it needs before it make
// possible, so the game can't get stuck whichever branch is taken first;
// everything each one unlocks can be made once it's complete; and
// everything is reachable once they all are.
void test_no_dead_ends() {
    const std::vector<Delivery>& deliveries = all_deliveries();
    for (std::size_t d = 0; d < deliveries.size(); ++d) {
        std::set<std::size_t> done = needed_before(d);
        for (const ItemStack& s : deliveries[d].wants) {
            if (!reachable(s.kind, done))
                std::printf("%s can't get %s\n", deliveries[d].name, item_name(s.kind));
            assert(reachable(s.kind, done));
        }
        done.insert(d);
        for (const ItemKind kind : deliveries[d].unlocks) {
            if (!reachable(kind, done))
                std::printf("%s unlocks %s, which can't be made yet\n", deliveries[d].name, item_name(kind));
            assert(reachable(kind, done));
        }
    }
    std::set<std::size_t> all;
    for (std::size_t d = 0; d < deliveries.size(); ++d)
        all.insert(d);
    for (int i = 0; i < kItemKindCount; ++i) {
        const ItemKind kind = static_cast<ItemKind>(i);
        if (is_layout_part(kind) || is_weapon(kind) || weapon_unlocking(kind))
            continue;  // comes with a ship's layout, or is found (or comes with what is), never made
        if (!reachable(kind, all))
            std::printf("unreachable: %s\n", item_name(kind));
        assert(reachable(kind, all));
    }
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

// Everything delivery `d` asks for, in raw resources, all kinds together.
double raw_total(std::size_t d) {
    double total = 0.0;
    for (const ItemStack& s : all_deliveries()[d].wants)
        for (const double part : raw_cost(s.kind, s.count))
            total += part;
    return total;
}

// How many crafting steps `kind` is from raw resources (0 for one).
int chain_depth(ItemKind kind) {
    if (raw_resource_index(kind))
        return 0;
    int deepest = 0;
    for (const ItemStack& in : recipe_for(kind)->inputs)
        deepest = std::max(deepest, chain_depth(in.kind));
    return deepest + 1;
}

int deepest_wanted(std::size_t d) {
    int deepest = 0;
    for (const ItemStack& s : all_deliveries()[d].wants)
        deepest = std::max(deepest, chain_depth(s.kind));
    return deepest;
}

// The deliveries grow, in a balanced way: each asks for more raw resources
// than any it comes after, but not more than three times as much (no
// sudden wall), and for items at least as far up the production chain;
// the last ones reach further up it than the first.
void test_balance() {
    const std::vector<Delivery>& deliveries = all_deliveries();
    for (std::size_t d = 0; d < deliveries.size(); ++d)
        for (const std::size_t before : delivery_prerequisites(d)) {
            const double ratio = raw_total(d) / raw_total(before);
            if (ratio <= 1.0 || ratio > 3.0)
                std::printf("%s asks %.2f times what %s does\n", deliveries[d].name, ratio, deliveries[before].name);
            assert(ratio > 1.0 && ratio <= 3.0);
            assert(deepest_wanted(d) >= deepest_wanted(before));
        }
    assert(deepest_wanted(deliveries.size() - 1) > deepest_wanted(0));
    // And each, from a home sector holding nothing but its own, could be
    // paid for twice over by a fresh sector's reserves of every kind it
    // needs (the sectors that take them hold that much, or more).
    for (std::size_t d = 0; d < deliveries.size(); ++d) {
        std::array<double, kRawResourceCount> cost{};
        for (const ItemStack& s : deliveries[d].wants) {
            const std::array<double, kRawResourceCount> part = raw_cost(s.kind, s.count);
            for (std::size_t i = 0; i < cost.size(); ++i)
                cost[i] += part[i];
        }
        for (std::size_t i = 0; i < cost.size(); ++i)
            assert(cost[i] * 2.0 <= kSectorReserves[i]);
    }
}

// Open deliveries, branching, delivering (capped at what's wanted, and
// only toward open ones), and completing one unlocking its recipes and
// opening the next.
void test_progress() {
    Progress progress;
    const std::size_t first = delivery_index("First light");
    const std::size_t heavy = delivery_index("Heavy industry");
    const std::size_t lines = delivery_index("Supply lines");
    const std::size_t deep = delivery_index("Deep space");
    assert(progress.open_deliveries() == std::vector<std::size_t>{first});
    assert(!progress.unlocked(ItemKind::Steel) && progress.unlocked(ItemKind::FuelCell));
    assert(progress.done_count() == 0 && !progress.all_complete());

    // Nothing toward one that isn't open, or of what it doesn't want.
    assert(!progress.deliver(heavy, ItemKind::Steel) && progress.delivered(heavy, ItemKind::Steel) == 0);
    assert(progress.wanted(heavy, ItemKind::Steel) == 0);
    assert(!progress.deliver(first, ItemKind::Steel) && progress.delivered(first, ItemKind::Steel) == 0);

    const std::vector<ItemStack>& wants = all_deliveries()[first].wants;
    // Past what's wanted doesn't count.
    assert(!progress.deliver(first, wants[0].kind, wants[0].count + 10));
    assert(progress.delivered(first, wants[0].kind) == wants[0].count);
    assert(progress.wanted(first, wants[0].kind) == 0);
    for (std::size_t i = 1; i < wants.size(); ++i) {
        assert(!progress.deliver(first, wants[i].kind, wants[i].count - 1));
        assert(progress.wanted(first, wants[i].kind) == 1);
    }
    for (std::size_t i = 1; i + 1 < wants.size(); ++i)
        assert(!progress.deliver(first, wants[i].kind));
    assert(progress.deliver(first, wants.back().kind));  // the last one in
    assert(progress.done(first) && !progress.open(first) && progress.done_count() == 1);
    for (const ItemKind kind : all_deliveries()[first].unlocks)
        assert(progress.unlocked(kind));
    assert(!progress.deliver(first, wants[0].kind));  // done is done

    // Two branches open now; the one after both waits for both.
    assert((progress.open_deliveries() == std::vector<std::size_t>{heavy, lines}));
    assert(progress.complete(heavy));
    assert(!progress.open(deep) && !progress.complete(deep));
    assert(progress.complete(lines));
    assert(progress.open(deep));
    assert(!layout_unlocked(all_layouts()[1], progress));
    assert(progress.complete(deep));
    assert(layout_unlocked(all_layouts()[1], progress));

    while (!progress.open_deliveries().empty())
        assert(progress.complete(progress.open_deliveries().front()));
    assert(progress.all_complete());
    // Everything but weapons and what only finding one unlocks (see
    // test_combat).
    const auto found_only = [](ItemKind kind) { return is_weapon(kind) || weapon_unlocking(kind).has_value(); };
    for (int i = 0; i < kItemKindCount; ++i)
        assert(progress.unlocked(static_cast<ItemKind>(i)) == !found_only(static_cast<ItemKind>(i)));
    for (const ShipLayout& layout : all_layouts())
        assert(layout_unlocked(layout, progress));
}

// Fills the provider up with `kind`, as belts feeding it flat out would.
void top_up(Provider& provider, ItemKind kind) {
    while (provider.accepts(kind))
        provider.receive(kind);
}

// A provider on a ship in the sector that takes a delivery hands over what
// it wants, an item every kLinkTicks, using it up; one anywhere else, or
// holding what the delivery doesn't want, hands over nothing. The last
// item completes it, as an event.
void test_provider_delivers() {
    World world;
    FleetShip& flagship = world.get_fleet().flagship();
    Ship& ship = flagship.ship;
    const Progress& progress = world.get_fleet().get_progress();
    const std::size_t first = delivery_index("First light");
    const int sector = world.get_map().delivery_sector(first);
    assert(sector > 0);

    ship.inventory().add(ItemKind::Provider, 1);
    GridPos at{};
    bool placed = false;
    for (int y = 0; y < ship.size() && !placed; ++y)
        for (int x = 0; x < ship.size() && !placed; ++x)
            if (ship.can_place(ItemKind::Provider, {x, y}) == BuildResult::Ok) {
                at = {x, y};
                placed = ship.place(ItemKind::Provider, at) == BuildResult::Ok;
            }
    assert(placed);
    auto& provider = dynamic_cast<Provider&>(*ship.building_anchored_at(at));

    // At home, nothing goes.
    top_up(provider, ItemKind::IronPlate);
    for (Tick t = 0; t < kTicksPerSecond; ++t)
        world.advance_tick();
    assert(progress.delivered(first, ItemKind::IronPlate) == 0);
    assert(provider.get_stored().count(ItemKind::IronPlate) == kLinkBuffer);

    // In the sector that takes it: an item every kLinkTicks, used up.
    flagship.sector = sector;
    const Tick run = 20 * kLinkTicks;
    for (Tick t = 0; t < run; ++t) {
        top_up(provider, ItemKind::IronPlate);
        world.advance_tick();
    }
    const int went = progress.delivered(first, ItemKind::IronPlate);
    assert(went >= 19 && went <= 21);

    // What it doesn't want stays put.
    while (provider.get_stored().count(ItemKind::IronPlate) > 0)
        world.advance_tick();
    top_up(provider, ItemKind::Steel);
    for (Tick t = 0; t < kTicksPerSecond; ++t)
        world.advance_tick();
    assert(provider.get_stored().count(ItemKind::Steel) == kLinkBuffer);
    assert(progress.delivered(first, ItemKind::Steel) == 0);
    while (provider.get_stored().count(ItemKind::Steel) > 0)
        provider.sent(ItemKind::Steel);

    // All but the last gear in; the provider's last gear completes it.
    Progress& shared = world.get_fleet().get_progress();
    for (const ItemStack& s : all_deliveries()[first].wants)
        shared.deliver(first, s.kind, s.kind == ItemKind::Gear ? s.count - 1 : s.count);
    assert(!progress.done(first));
    world.take_events();
    provider.receive(ItemKind::Gear);
    provider.receive(ItemKind::Gear);
    for (Tick t = 0; t < 2 * kLinkTicks; ++t)
        world.advance_tick();
    assert(progress.done(first) && progress.unlocked(ItemKind::Steel));
    assert(provider.get_stored().count(ItemKind::Gear) == 1);  // nothing more wanted here
    const std::vector<Event> events = world.take_events();
    const auto done = std::count_if(events.begin(), events.end(), [&](const Event& e) {
        return e.kind == EventKind::DeliveryComplete && e.delivery == static_cast<int>(first) && e.sector == sector &&
               e.ship == flagship.id;
    });
    assert(done == 1);
}

// Hops from home to each sector.
std::vector<int> hops_from_home(const StarMap& map) {
    std::vector<int> hops(static_cast<std::size_t>(map.size()), -1);
    hops[0] = 0;
    for (std::deque<int> queue{0}; !queue.empty(); queue.pop_front())
        for (const int j : map.at(queue.front()).neighbors)
            if (hops[static_cast<std::size_t>(j)] < 0) {
                hops[static_cast<std::size_t>(j)] = hops[static_cast<std::size_t>(queue.front())] + 1;
                queue.push_back(j);
            }
    return hops;
}

bool safe(const Sector& s) {
    return s.threat == 0 && !s.enemy;
}

// On every map: kSafeSectors safe sectors around home, home's neighbors
// among them, joined to home through safe sectors only; each delivery is
// taken in one of them, never home, the first next to home, the later
// ones no nearer than those they come after, and those open at the same
// time in different sectors.
void test_map_placement() {
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        const StarMap map = StarMap::generate(seed);
        const std::vector<int> hops = hops_from_home(map);
        int safe_count = 0;
        for (int i = 1; i < map.size(); ++i)
            safe_count += safe(map.at(i)) ? 1 : 0;
        assert(safe_count >= kSafeSectors);
        for (const int n : map.at(0).neighbors)
            assert(safe(map.at(n)));
        for (std::size_t d = 0; d < all_deliveries().size(); ++d) {
            const int s = map.delivery_sector(d);
            assert(s > 0 && s < map.size() && safe(map.at(s)));
            // A safe way there: some safe neighbor one hop nearer home.
            for (int at = s; at != 0;) {
                const std::vector<int>& n = map.at(at).neighbors;
                const auto nearer = std::find_if(n.begin(), n.end(), [&](int j) {
                    return hops[static_cast<std::size_t>(j)] == hops[static_cast<std::size_t>(at)] - 1 &&
                           (j == 0 || safe(map.at(j)));
                });
                assert(nearer != n.end());
                at = *nearer;
            }
            for (const std::size_t before : delivery_prerequisites(d))
                assert(hops[static_cast<std::size_t>(s)] >=
                       hops[static_cast<std::size_t>(map.delivery_sector(before))]);
            for (std::size_t e = 0; e < d; ++e)
                if (delivery_tier(e) == delivery_tier(d))
                    assert(map.delivery_sector(e) != s);
            assert(map.deliveries_at(s).end() !=
                   std::find(map.deliveries_at(s).begin(), map.deliveries_at(s).end(), d));
        }
        assert(map.linked(0, map.delivery_sector(0)));
    }
}

// Fuel comes first: fuel cells are made in a fabricator Mk1 (which a new
// ship can make by hand) from carbon alone, unlocked from the start, as is
// the provider; a skiff's first jump takes a small part of home's carbon.
void test_travel_from_the_start() {
    const Progress progress;
    const Recipe& fuel = *recipe_for(ItemKind::FuelCell);
    assert(progress.unlocked(fuel) && machine_crafts(ItemKind::FabricatorMk1, fuel.category));
    assert(recipe_for(ItemKind::FabricatorMk1)->hand_craftable);
    const auto per_cell = raw_cost(ItemKind::FuelCell, 1.0);
    for (int r = 0; r < kRawResourceCount; ++r)
        assert(per_cell[static_cast<std::size_t>(r)] == 0.0 || kRawResources[r] == ItemKind::Carbon);
    const auto jump = raw_cost(ItemKind::FuelCell,
                               static_cast<double>(kThrusterCharge * starter_layout().engines.size()));
    const auto carbon = static_cast<std::size_t>(*raw_resource_index(ItemKind::Carbon));
    assert(jump[carbon] * 10.0 <= kHomeReserves[carbon]);

    const Recipe& provider = *recipe_for(ItemKind::Provider);
    assert(progress.unlocked(provider) && provider.hand_craftable);
    for (const ItemStack& in : provider.inputs)
        assert(progress.unlocked(in.kind) || item_category(in.kind) == ItemCategory::RawResource);
}

}  // namespace

int main() {
    test_table();
    test_no_dead_ends();
    test_balance();
    test_progress();
    test_provider_delivers();
    test_map_placement();
    test_travel_from_the_start();
    std::puts("test_deliveries: all passed");
    return 0;
}
