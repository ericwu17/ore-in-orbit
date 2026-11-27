// test_deliveries.cpp — headless tests for the deliveries and the techs
// their points buy: the tables (chains of deliveries, each asking for more,
// and further up the production chain, than the one before it; techs each
// after those listed before them, unlocking what can then be made), the
// economy (a greedy player buys every tech and makes every delivery),
// progress (open deliveries, paying out, the shop), providers making them
// in the sector that takes them (and nowhere else), where the map puts
// them (a safe sector per chain, the first next to home), and travel from
// the very start: fuel cells from a fabricator, and providers.
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
#include <cmath>
#include <cstdio>
#include <deque>
#include <set>

namespace {

void test_table() {
    const std::vector<Delivery>& deliveries = all_deliveries();
    assert(deliveries.size() >= 6);
    for (std::size_t d = 0; d < deliveries.size(); ++d) {
        const Delivery& delivery = deliveries[d];
        assert(!delivery.wants.empty());
        assert(delivery.chain >= 0 && delivery.chain < kDeliveryChains);
        for (const ItemStack& s : delivery.wants)
            assert(s.count > 0 && item_category(s.kind) != ItemCategory::Placeable);
        // Names are unique, so tables can name a delivery (delivery_index).
        assert(delivery_index(delivery.name) == d);
        // Pays at least a point.
        assert(delivery_points(d) >= 1);
    }
    // Every chain has deliveries, each after the one listed before it.
    for (int chain = 0; chain < kDeliveryChains; ++chain) {
        const std::vector<std::size_t> in_chain = chain_deliveries(chain);
        assert(in_chain.size() >= 2);
        assert(!delivery_before(in_chain.front()));
        for (std::size_t i = 1; i < in_chain.size(); ++i)
            assert(delivery_before(in_chain[i]) == in_chain[i - 1]);
    }

    const std::vector<Tech>& techs = all_techs();
    assert(techs.size() >= 4);
    std::array<int, kItemKindCount> unlocked_by{};
    std::size_t roots = 0;
    for (std::size_t t = 0; t < techs.size(); ++t) {
        const Tech& tech = techs[t];
        // Each unlocks something: recipes, or a ship layout. Cheap, and
        // says what it's for.
        const bool unlocks_layout = std::any_of(all_layouts().begin(), all_layouts().end(),
                                                [&](const ShipLayout& l) { return l.tech == t; });
        assert(tech.cost > 0 && tech.cost <= 50 && (!tech.unlocks.empty() || unlocks_layout));
        assert(tech.blurb && *tech.blurb);
        assert(tech_tier(t) >= 1);
        for (const std::size_t before : tech_prerequisites(t))
            assert(tech_tier(before) < tech_tier(t));
        assert(tech_index(tech.name) == t);
        // Each item is unlocked once, and only items with a recipe.
        for (const ItemKind kind : tech.unlocks) {
            assert(recipe_for(kind) && !weapon_unlocking(kind));
            assert(++unlocked_by[static_cast<std::size_t>(kind)] == 1);
            assert(tech_unlocking(kind) == t);
        }
        // Each comes after techs listed before it.
        for (const std::size_t before : tech_prerequisites(t))
            assert(before < t);
        roots += tech_prerequisites(t).empty() ? 1u : 0u;
    }
    // A wide tree: several to choose from at the start, several tiers with
    // three or more side by side, and more than twice as many techs as
    // tiers.
    assert(roots >= 3);
    std::array<int, 16> per_tier{};
    int deepest = 0;
    for (std::size_t t = 0; t < techs.size(); ++t) {
        ++per_tier[static_cast<std::size_t>(tech_tier(t))];
        deepest = std::max(deepest, tech_tier(t));
    }
    assert(std::count_if(per_tier.begin(), per_tier.end(), [](int n) { return n >= 3; }) >= 2);
    assert(static_cast<int>(techs.size()) > 2 * deepest);
    // What a new ship makes, and travel, need no tech.
    for (const ItemKind kind : {ItemKind::TransportBelt, ItemKind::AsteroidCollector, ItemKind::SolarFurnace,
                                ItemKind::FabricatorMk1, ItemKind::IronPlate, ItemKind::CopperPlate, ItemKind::Gear,
                                ItemKind::Circuit, ItemKind::Glass, ItemKind::FuelCell, ItemKind::Provider})
        assert(!tech_unlocking(kind));
    // Branches: two techs come after the same one, for the player to pick
    // between.
    bool branches = false;
    for (std::size_t a = 0; a < techs.size(); ++a)
        for (std::size_t b = a + 1; b < techs.size(); ++b)
            branches = branches || (!tech_prerequisites(a).empty() && tech_prerequisites(a) == tech_prerequisites(b));
    assert(branches);
    // Every bigger ship layout comes with a tech.
    for (const ShipLayout& layout : all_layouts())
        assert(&layout == &starter_layout() ? !layout.tech : layout.tech && *layout.tech < techs.size());
}

// Tech `t` and every tech it comes after (theirs too), into `into`.
void with_prerequisites(std::size_t t, std::set<std::size_t>& into) {
    if (!into.insert(t).second)
        return;
    for (const std::size_t p : tech_prerequisites(t))
        with_prerequisites(p, into);
}

// The techs that have to be bought before delivery `d` can be open: those
// for what it, and every one before it in its chain, asks for.
std::set<std::size_t> techs_needed(std::size_t d) {
    std::set<std::size_t> needed;
    for (std::optional<std::size_t> e = d; e; e = delivery_before(*e))
        for (const ItemStack& s : all_deliveries()[*e].wants)
            if (const std::optional<std::size_t> t = tech_unlocking(s.kind))
                with_prerequisites(*t, needed);
    return needed;
}

// Whether `kind` can be had with only what's unlocked by the techs in
// `bought`: in a new game's inventory (which breaks the cycles: collectors
// are made from plates smelted from what they gather), gathered by an
// unlocked collector, or made by an unlocked recipe (run by hand or by an
// unlocked machine) from such items.
bool reachable(ItemKind kind, const std::set<std::size_t>& bought, int depth = 0) {
    assert(depth < 32);
    static const Ship start = Ship::new_game();
    if (start.inventory().count(kind) > 0)
        return true;
    auto unlocked = [&](ItemKind k) {
        const std::optional<std::size_t> at = tech_unlocking(k);
        return !at || bought.contains(*at);
    };
    if (item_category(kind) == ItemCategory::RawResource) {
        for (const ItemKind collector : {ItemKind::AsteroidCollector, ItemKind::TractorArray}) {
            const std::vector<ItemKind>& targets = collector_spec(collector).targets;
            if (unlocked(collector) && reachable(collector, bought, depth + 1) &&
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
        runnable = machine_crafts(machine, r->category) && unlocked(machine) && reachable(machine, bought, depth + 1);
    }
    if (!runnable)
        return false;
    for (const ItemStack& s : r->inputs)
        if (!reachable(s.kind, bought, depth + 1))
            return false;
    return true;
}

// Every delivery asks only for what the techs it needs make possible, so
// the game can't get stuck whichever is bought first; everything each tech
// unlocks can be made once it (and those it comes after) are bought; and
// everything is reachable once they all are.
void test_no_dead_ends() {
    const std::vector<Delivery>& deliveries = all_deliveries();
    for (std::size_t d = 0; d < deliveries.size(); ++d) {
        const std::set<std::size_t> bought = techs_needed(d);
        for (const ItemStack& s : deliveries[d].wants) {
            if (!reachable(s.kind, bought))
                std::printf("%s can't get %s\n", deliveries[d].name, item_name(s.kind));
            assert(reachable(s.kind, bought));
        }
    }
    for (std::size_t t = 0; t < all_techs().size(); ++t) {
        std::set<std::size_t> bought;
        with_prerequisites(t, bought);
        for (const ItemKind kind : all_techs()[t].unlocks) {
            if (!reachable(kind, bought))
                std::printf("%s unlocks %s, which can't be made yet\n", all_techs()[t].name, item_name(kind));
            assert(reachable(kind, bought));
        }
    }
    std::set<std::size_t> all;
    for (std::size_t t = 0; t < all_techs().size(); ++t)
        all.insert(t);
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
// than the one before it in its chain, but not more than three times as
// much (no sudden wall), and for items at least as far up the production
// chain; each chain's last reaches further up it than its first. And what
// they pay (delivery_points) follows what they take.
void test_balance() {
    const std::vector<Delivery>& deliveries = all_deliveries();
    for (std::size_t d = 0; d < deliveries.size(); ++d) {
        assert(std::abs(delivery_points(d) - std::max(1.0, raw_total(d) / kRawPerPoint)) <= 0.5);
        if (const std::optional<std::size_t> before = delivery_before(d)) {
            const double ratio = raw_total(d) / raw_total(*before);
            if (ratio <= 1.0 || ratio > 3.0)
                std::printf("%s asks %.2f times what %s does\n", deliveries[d].name, ratio,
                            deliveries[*before].name);
            assert(ratio > 1.0 && ratio <= 3.0);
            if (deepest_wanted(d) < deepest_wanted(*before))
                std::printf("%s (depth %d) is less far up than %s (depth %d)\n", deliveries[d].name,
                            deepest_wanted(d), deliveries[*before].name, deepest_wanted(*before));
            assert(deepest_wanted(d) >= deepest_wanted(*before));
            assert(delivery_points(d) > delivery_points(*before));
        }
    }
    for (int chain = 0; chain < kDeliveryChains; ++chain) {
        const std::vector<std::size_t> in_chain = chain_deliveries(chain);
        assert(deepest_wanted(in_chain.back()) > deepest_wanted(in_chain.front()));
    }
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

// The economy holds up: a player who makes every delivery they can, and
// buys the cheapest tech they can afford, buys every tech and makes every
// delivery, never stuck with nothing open and nothing to buy. The first
// tech is paid for by the deliveries open at the start, and the points
// left over at the end are a small part of what was earned (most of it
// buys something).
void test_economy() {
    Progress progress;
    int earned = 0;
    std::size_t rounds = 0;
    for (;; ++rounds) {
        assert(rounds < 100);
        bool did = false;
        for (const std::size_t d : progress.open_deliveries()) {
            const int before = progress.points();
            assert(progress.complete(d));
            assert(progress.points() == before + delivery_points(d));
            earned += delivery_points(d);
            did = true;
        }
        std::optional<std::size_t> cheapest;
        for (std::size_t t = 0; t < all_techs().size(); ++t)
            if (progress.can_buy(t) && (!cheapest || all_techs()[t].cost < all_techs()[*cheapest].cost))
                cheapest = t;
        if (cheapest) {
            const int before = progress.points();
            assert(progress.buy(*cheapest) && progress.bought(*cheapest));
            assert(progress.points() == before - all_techs()[*cheapest].cost);
            did = true;
        }
        if (rounds == 0)
            assert(cheapest.has_value());  // the first tech, from the first deliveries
        if (!did)
            break;
    }
    if (!progress.all_complete() || progress.bought_count() != all_techs().size())
        std::printf("stuck: %zu deliveries made, %zu techs bought, %d %s left\n", progress.done_count(),
                    progress.bought_count(), progress.points(), kPointsName);
    assert(progress.all_complete() && progress.bought_count() == all_techs().size());
    int spent = 0;
    for (const Tech& tech : all_techs())
        spent += tech.cost;
    assert(earned - spent == progress.points() && progress.points() >= 0);
    assert(progress.points() * 4 <= earned);
}

// Open deliveries, one per chain at a time, delivering (capped at what's
// wanted, and only toward open ones), completing one paying out and
// opening the next in its chain, and the shop: techs for sale once those
// they come after are bought, bought only with the points for them.
void test_progress() {
    Progress progress;
    const std::size_t hull = delivery_index("Hull plating");
    const std::size_t girders = delivery_index("Girders");
    const std::size_t steelworks = tech_index("Steelworks");
    const std::size_t refining = tech_index("Refining");
    // Each chain's first is open, except those asking for what needs a tech.
    for (int chain = 0; chain < kDeliveryChains; ++chain) {
        const std::size_t first = chain_deliveries(chain).front();
        const std::vector<ItemStack>& wants = all_deliveries()[first].wants;
        const bool needs_tech =
            std::any_of(wants.begin(), wants.end(), [](const ItemStack& s) { return tech_unlocking(s.kind); });
        assert(progress.open(first) == !needs_tech);
        assert(progress.chain_current(chain) == first);
        for (std::size_t i = 1; i < chain_deliveries(chain).size(); ++i)
            assert(!progress.open(chain_deliveries(chain)[i]));
    }
    assert(progress.open_deliveries().size() >= 3);
    assert(!progress.unlocked(ItemKind::Steel) && progress.unlocked(ItemKind::FuelCell));
    assert(progress.done_count() == 0 && !progress.all_complete() && progress.points() == 0);

    // Nothing toward one that isn't open, or of what it doesn't want.
    assert(!progress.deliver(girders, ItemKind::Gear) && progress.delivered(girders, ItemKind::Gear) == 0);
    assert(progress.wanted(girders, ItemKind::Gear) == 0);
    assert(!progress.deliver(hull, ItemKind::Steel) && progress.delivered(hull, ItemKind::Steel) == 0);

    const std::vector<ItemStack>& wants = all_deliveries()[hull].wants;
    // Past what's wanted doesn't count.
    assert(!progress.deliver(hull, wants[0].kind, wants[0].count + 10));
    assert(progress.delivered(hull, wants[0].kind) == wants[0].count);
    assert(progress.wanted(hull, wants[0].kind) == 0);
    for (std::size_t i = 1; i < wants.size(); ++i) {
        assert(!progress.deliver(hull, wants[i].kind, wants[i].count - 1));
        assert(progress.wanted(hull, wants[i].kind) == 1);
    }
    assert(progress.points() == 0);
    assert(progress.deliver(hull, wants.back().kind));  // the last one in
    assert(progress.done(hull) && !progress.open(hull) && progress.done_count() == 1);
    assert(progress.points() == delivery_points(hull));
    assert(!progress.deliver(hull, wants[0].kind));  // done is done
    assert(progress.points() == delivery_points(hull));

    // The next in the chain asks for steel: it waits for the tech.
    assert(progress.chain_current(0) == girders && !progress.open(girders));

    // The shop: only the root tech is for sale; the rest wait for it.
    assert(progress.for_sale(steelworks) && !progress.for_sale(refining));
    assert(!progress.buy(refining));
    progress.add_points(all_techs()[refining].cost);
    assert(!progress.can_buy(refining) && !progress.buy(refining));
    const int points = progress.points();
    assert(progress.can_buy(steelworks) && progress.buy(steelworks));
    assert(progress.points() == points - all_techs()[steelworks].cost);
    assert(progress.bought(steelworks) && !progress.for_sale(steelworks) && !progress.buy(steelworks));
    for (const ItemKind kind : all_techs()[steelworks].unlocks)
        assert(progress.unlocked(kind));
    assert(progress.open(girders));
    assert(progress.for_sale(refining));
    // Not enough points: nothing changes.
    const int left = progress.points();
    if (left < all_techs()[refining].cost) {
        assert(!progress.can_buy(refining) && !progress.buy(refining) && progress.points() == left);
    }
    // Granting needs no points, but the order still holds.
    assert(!progress.grant(tech_index("Starforge")));
    assert(progress.grant(refining) && progress.points() == left && progress.bought(refining));

    // Layouts come with techs.
    assert(!layout_unlocked(all_layouts()[1], progress));
    assert(progress.grant(tech_index("Structures")) && progress.grant(tech_index("Microelectronics")));
    assert(progress.grant(tech_index("Sorting")) && progress.grant(tech_index("Logistics")));
    assert(progress.grant(tech_index("Shipwright")));
    assert(layout_unlocked(all_layouts()[1], progress));
    // What a tech opens.
    const std::vector<std::size_t> opened = deliveries_opened_by(steelworks);
    assert(std::find(opened.begin(), opened.end(), girders) != opened.end());
    assert(std::find(opened.begin(), opened.end(), hull) == opened.end());

    for (std::size_t t = 0; t < all_techs().size(); ++t)
        progress.grant(t);
    assert(progress.bought_count() == all_techs().size());
    while (!progress.open_deliveries().empty())
        assert(progress.complete(progress.open_deliveries().front()));
    assert(progress.all_complete());
    for (int chain = 0; chain < kDeliveryChains; ++chain)
        assert(!progress.chain_current(chain));
    // Everything but weapons and what only finding one unlocks (see
    // test_combat).
    const auto found_only = [](ItemKind kind) { return is_weapon(kind) || weapon_unlocking(kind).has_value(); };
    for (int i = 0; i < kItemKindCount; ++i)
        assert(progress.unlocked(static_cast<ItemKind>(i)) == !found_only(static_cast<ItemKind>(i)));
    for (const ShipLayout& layout : all_layouts())
        assert(layout_unlocked(layout, progress));
}

// Feeds the provider `kind` if it takes one now, as a belt feeding it flat
// out would.
void top_up(Provider& provider, ItemKind kind) {
    if (provider.accepts(kind))
        provider.receive(kind);
}

// A provider on a ship in the sector that takes a delivery passes what it
// wants straight to it, an item every kLinkTicks, using it up; one anywhere
// else takes nothing. It holds nothing: it takes only what the delivery
// still wants and what requesters in its sector are ready for, so a
// surplus can't jam it. The last item completes it, as an event.
void test_provider_delivers() {
    World world;
    FleetShip& flagship = world.get_fleet().flagship();
    Ship& ship = flagship.ship;
    const Progress& progress = world.get_fleet().get_progress();
    const std::size_t first = delivery_index("Hull plating");
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
    const auto idle = [&] {
        for (Tick t = 0; t < kLinkTicks; ++t)
            world.advance_tick();
    };

    // At home, nothing asks for it: it takes none, and nothing goes.
    const int plates = ship.inventory().count(ItemKind::IronPlate);
    for (Tick t = 0; t < kTicksPerSecond; ++t) {
        top_up(provider, ItemKind::IronPlate);
        world.advance_tick();
    }
    assert(!provider.accepts(ItemKind::IronPlate) && provider.contents().empty());
    assert(progress.delivered(first, ItemKind::IronPlate) == 0);
    assert(ship.inventory().count(ItemKind::IronPlate) == plates);

    // In the sector that takes it: an item every kLinkTicks, used up.
    flagship.sector = sector;
    world.advance_tick();
    const Tick run = 20 * kLinkTicks;
    for (Tick t = 0; t < run; ++t) {
        top_up(provider, ItemKind::IronPlate);
        world.advance_tick();
        assert(provider.contents().empty());  // passed on within the tick
    }
    const int went = progress.delivered(first, ItemKind::IronPlate);
    assert(went >= 19 && went <= 21);

    // Here, it doesn't take what the delivery doesn't want.
    idle();
    assert(!provider.accepts(ItemKind::Steel) && provider.accepts(ItemKind::Gear));

    // Once the iron plates are all in, it takes no more of them: a surplus
    // can't keep the gears out.
    Progress& shared = world.get_fleet().get_progress();
    shared.deliver(first, ItemKind::IronPlate, progress.wanted(first, ItemKind::IronPlate));
    world.advance_tick();
    assert(!provider.accepts(ItemKind::IronPlate) && provider.accepts(ItemKind::Gear));

    // What a requester in its sector is ready for, it takes too.
    ship.inventory().add(ItemKind::Requester, 1);
    GridPos req{};
    bool req_placed = false;
    for (int y = 0; y < ship.size() && !req_placed; ++y)
        for (int x = 0; x < ship.size() && !req_placed; ++x)
            if (ship.can_place(ItemKind::Requester, {x, y}) == BuildResult::Ok) {
                req = {x, y};
                req_placed = ship.place(ItemKind::Requester, req) == BuildResult::Ok;
            }
    assert(req_placed);
    assert(ship.set_filter(req, ItemKind::Steel));
    world.advance_tick();
    assert(provider.accepts(ItemKind::Steel) && !provider.accepts(ItemKind::IronPlate));
    // Away from any delivery, only that.
    flagship.sector = 0;
    world.advance_tick();
    assert(provider.accepts(ItemKind::Steel) && !provider.accepts(ItemKind::Gear) &&
           !provider.accepts(ItemKind::IronPlate));
    flagship.sector = sector;
    assert(ship.remove_building(req) == BuildResult::Ok);
    world.advance_tick();

    // All but the last gear in; the provider's last gear completes it, the
    // tick it takes it.
    shared.deliver(first, ItemKind::Gear, progress.wanted(first, ItemKind::Gear) - 1);
    world.advance_tick();
    assert(!progress.done(first));
    world.take_events();
    assert(provider.accepts(ItemKind::Gear));
    provider.receive(ItemKind::Gear);
    world.advance_tick();
    assert(progress.done(first) && progress.points() == delivery_points(first));
    const std::vector<Event> events = world.take_events();
    const auto done = std::count_if(events.begin(), events.end(), [&](const Event& e) {
        return e.kind == EventKind::DeliveryComplete && e.delivery == static_cast<int>(first) && e.sector == sector &&
               e.ship == flagship.id;
    });
    assert(done == 1);
    // Then nothing more is wanted here: it takes no more gears.
    idle();
    assert(!provider.accepts(ItemKind::Gear));

    // An item it was fed when nothing asked for it (between ticks, here)
    // goes to its bridge, not lost.
    const int gears = ship.inventory().count(ItemKind::Gear);
    provider.receive(ItemKind::Gear);
    world.advance_tick();
    assert(provider.contents().empty() && ship.inventory().count(ItemKind::Gear) == gears + 1);
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
// among them, joined to home through safe sectors only; each chain of
// deliveries is taken in one of them, never home, a different one per
// chain, the first chain's next to home.
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
            // A chain in one sector; other chains elsewhere.
            for (std::size_t e = 0; e < d; ++e)
                assert((map.delivery_sector(e) == s) == (all_deliveries()[e].chain == all_deliveries()[d].chain));
            const std::vector<std::size_t> here = map.deliveries_at(s);
            assert(std::find(here.begin(), here.end(), d) != here.end());
        }
        assert(map.linked(0, map.delivery_sector(chain_deliveries(0).front())));
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
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // what failed, before an assert aborts
    test_table();
    test_no_dead_ends();
    test_balance();
    test_economy();
    test_progress();
    test_provider_delivers();
    test_map_placement();
    test_travel_from_the_start();
    std::puts("test_deliveries: all passed");
    return 0;
}
