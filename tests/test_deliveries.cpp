// test_deliveries.cpp — headless tests for the deliveries and the techs
// their points buy: the deliveries each game makes up (finite chains, each
// asking for more, and for items needing later techs, than the one before
// it; endless chains, a standing order after each one made, growing with
// the points earned all game), the tech table (techs each after those
// listed before them, unlocking what can then be made), the economy (a
// greedy player buys every tech, the finite deliveries alone paying for
// part of the tree), progress (open deliveries, paying out, the shop),
// providers making them in the sector that takes them (and nowhere else),
// where the map puts them (a safe sector per chain, the first next to
// home), and travel from the very start: fuel cells from a fabricator, and
// providers.
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
#include <string>

namespace {

// The seeds the generator is tried with.
constexpr std::uint64_t kSeeds = 60;

// Every item it may ask for is something to deliver: no building, weapon or
// ammunition only a found weapon unlocks; and each finite chain's first and
// each endless chain's whole line has something made from the start.
void test_themes_and_techs() {
    int endless = 0;
    for (int chain = 0; chain < kDeliveryChains; ++chain) {
        endless += chain_endless(chain) ? 1 : 0;
        assert(chain_role(chain) && *chain_role(chain));
        const std::vector<ItemKind> items = chain_items(chain);
        assert(items.size() >= 3);
        for (const ItemKind kind : items)
            assert(item_category(kind) != ItemCategory::Placeable && !is_weapon(kind) && !weapon_unlocking(kind) &&
                   recipe_for(kind));
        assert(std::any_of(items.begin(), items.end(), [](ItemKind k) { return !tech_unlocking(k); }));
    }
    // Some sectors keep ordering for good, some don't.
    assert(endless >= 1 && endless < kDeliveryChains);

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

// The highest tech tier anything `wants` needs (0: none).
int tier_needed(const std::vector<ItemStack>& wants) {
    int tier = 0;
    for (const ItemStack& s : wants)
        if (const std::optional<std::size_t> t = tech_unlocking(s.kind))
            tier = std::max(tier, tech_tier(*t));
    return tier;
}

// The deliveries each game makes up: every finite chain's, in order, and
// each endless chain's first, open from the start; each asking for a few
// distinct items, in whole fives, and paying what its goods take. The same
// seed makes the same ones; other seeds, others.
void test_made_up() {
    std::set<std::string> names;
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
        const Progress progress(seed);
        for (int chain = 0; chain < kDeliveryChains; ++chain) {
            const std::vector<std::size_t> in_chain = progress.chain_deliveries(chain);
            assert(in_chain.size() == (chain_endless(chain) ? 1u : static_cast<std::size_t>(kFiniteChainLength)));
            assert(!progress.delivery_before(in_chain.front()));
            for (std::size_t i = 1; i < in_chain.size(); ++i)
                assert(progress.delivery_before(in_chain[i]) == in_chain[i - 1]);
            assert(progress.chain_current(chain) == in_chain.front());
            if (chain_endless(chain))
                assert(progress.open(in_chain.front()));
        }
        const Progress again(seed);
        assert(again.delivery_count() == progress.delivery_count());
        for (std::size_t d = 0; d < progress.delivery_count(); ++d) {
            const Delivery& delivery = progress.delivery(d);
            assert(!delivery.name.empty() && !delivery.wants.empty() && delivery.wants.size() <= 3);
            assert(delivery.chain >= 0 && delivery.chain < kDeliveryChains);
            const std::vector<ItemKind> items = chain_items(delivery.chain);
            for (const ItemStack& s : delivery.wants) {
                assert(s.count >= 5 && s.count % 5 == 0);
                assert(std::find(items.begin(), items.end(), s.kind) != items.end());
                assert(std::count_if(delivery.wants.begin(), delivery.wants.end(),
                                     [&](const ItemStack& w) { return w.kind == s.kind; }) == 1);
            }
            assert(delivery.points >= 1 && delivery.points == delivery_points(delivery.wants));
            assert(again.delivery(d).name == delivery.name && again.delivery(d).wants == delivery.wants);
            names.insert(delivery.name);
        }
    }
    assert(names.size() > 40);  // they vary from game to game
}

// The techs that have to be bought before finite delivery `d` can be open:
// those for what it, and every one before it in its chain, asks for.
std::set<std::size_t> techs_needed(const Progress& progress, std::size_t d) {
    std::set<std::size_t> needed;
    for (std::optional<std::size_t> e = d; e; e = progress.delivery_before(*e))
        for (const ItemStack& s : progress.delivery(*e).wants)
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
// the game can't get stuck whichever is bought first (an endless one asks
// only for what's unlocked when it's made up); everything each tech
// unlocks can be made once it (and those it comes after) are bought; and
// everything is reachable once they all are.
void test_no_dead_ends() {
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
        const Progress progress(seed);
        for (std::size_t d = 0; d < progress.delivery_count(); ++d) {
            const std::set<std::size_t> bought = techs_needed(progress, d);
            for (const ItemStack& s : progress.delivery(d).wants) {
                if (!reachable(s.kind, bought))
                    std::printf("%s can't get %s\n", progress.delivery(d).name.c_str(), item_name(s.kind));
                assert(reachable(s.kind, bought));
            }
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

// Everything `wants` asks for, in raw resources, all kinds together.
double raw_total(const std::vector<ItemStack>& wants) {
    double total = 0.0;
    for (const ItemStack& s : wants)
        for (const double part : raw_cost(s.kind, s.count))
            total += part;
    return total;
}

// The finite chains grow, in a balanced way: each delivery pays about what
// its step aims for, asks for more raw resources than the one before it in
// its chain, but not more than three times as much (no sudden wall), and
// needs techs at least as far up the tree; each chain's last needs a later
// tech than its first. And each could be paid for twice over by a fresh
// sector's reserves of every kind it needs.
void test_balance() {
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
        const Progress progress(seed);
        for (int chain = 0; chain < kDeliveryChains; ++chain) {
            if (chain_endless(chain))
                continue;
            const std::vector<std::size_t> in_chain = progress.chain_deliveries(chain);
            for (std::size_t i = 0; i < in_chain.size(); ++i) {
                const Delivery& d = progress.delivery(in_chain[i]);
                assert(std::abs(d.points - std::max(1.0, raw_total(d.wants) / kRawPerPoint)) <= 0.5);
                const int aim = kFiniteStepPoints[i];
                if (std::abs(d.points - aim) > std::max(2, aim / 4))
                    std::printf("%s pays %d, aiming for %d\n", d.name.c_str(), d.points, aim);
                assert(std::abs(d.points - aim) <= std::max(2, aim / 4));
                std::array<double, kRawResourceCount> cost{};
                for (const ItemStack& s : d.wants) {
                    const std::array<double, kRawResourceCount> part = raw_cost(s.kind, s.count);
                    for (std::size_t r = 0; r < cost.size(); ++r)
                        cost[r] += part[r];
                }
                for (std::size_t r = 0; r < cost.size(); ++r)
                    assert(cost[r] * 2.0 <= kSectorReserves[r]);
                if (i == 0) {
                    assert(tier_needed(d.wants) <= 1);  // open from the start, or after a first tech
                    continue;
                }
                const Delivery& before = progress.delivery(in_chain[i - 1]);
                const double ratio = raw_total(d.wants) / raw_total(before.wants);
                if (ratio <= 1.0 || ratio > 3.0)
                    std::printf("%s asks %.2f times what %s does\n", d.name.c_str(), ratio, before.name.c_str());
                assert(ratio > 1.0 && ratio <= 3.0);
                assert(d.points > before.points);
                assert(tier_needed(d.wants) >= tier_needed(before.wants));
            }
            assert(tier_needed(progress.delivery(in_chain.back()).wants) >
                   tier_needed(progress.delivery(in_chain.front()).wants));
        }
    }
}

// Endless chains: a standing order made, the next is made up at once, open
// (asking only for what can be made by then), and named in turn; each aims
// to pay endless_target_points of everything earned so far, so they grow
// as the game goes on; and as techs unlock better goods, they ask for them.
void test_endless() {
    assert(endless_target_points(0) == kEndlessBasePoints);
    assert(endless_target_points(1000) == kEndlessBasePoints + 100);
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
        Progress progress(seed);
        int chain = 0;
        while (!chain_endless(chain))
            ++chain;
        int last_points = 0;
        for (int n = 1; n <= 12; ++n) {
            const std::size_t d = *progress.chain_current(chain);
            const Delivery delivery = progress.delivery(d);
            assert(progress.open(d) && progress.chain_deliveries(chain).back() == d);
            assert(delivery.name.ends_with(" #" + std::to_string(n)));
            // About what it aims for (its goods are rounded to fives).
            const int aim = endless_target_points(progress.earned());
            assert(std::abs(delivery.points - aim) <= std::max(2, aim / 4));
            assert(delivery.points >= last_points || n == 1 || std::abs(delivery.points - last_points) <= 3);
            last_points = delivery.points;
            const long long earned = progress.earned();
            const int points = progress.points();
            assert(progress.complete(d));
            assert(progress.earned() == earned + delivery.points && progress.points() == points + delivery.points);
            assert(progress.chain_deliveries(chain).size() == static_cast<std::size_t>(n) + 1);
            // Points from elsewhere count toward the next too.
            progress.add_points(20);
        }
        // Spending doesn't shrink what was earned.
        const long long earned = progress.earned();
        assert(progress.buy(tech_index("Steelworks")) && progress.earned() == earned);
        // Once everything is unlocked, its main good is one of the two most
        // advanced it takes.
        for (std::size_t t = 0; t < all_techs().size(); ++t)
            progress.grant(t);
        assert(progress.complete(*progress.chain_current(chain)));
        const Delivery& next = progress.delivery(*progress.chain_current(chain));
        assert(tier_needed({next.wants.front()}) >= 2);
        for (const ItemStack& s : next.wants)
            assert(progress.unlocked(s.kind));
    }
}

// The economy holds up: a player who makes every delivery they can, and
// buys the cheapest tech they can afford, buys every tech, never stuck
// with nothing open and nothing to buy; the first tech is paid for by the
// deliveries open at the start. The finite chains alone don't pay for the
// whole tree: the standing orders are needed for the rest.
void test_economy() {
    int spent = 0;
    for (const Tech& tech : all_techs())
        spent += tech.cost;
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
        Progress progress(seed);
        int finite = 0;
        for (std::size_t d = 0; d < progress.delivery_count(); ++d)
            finite += chain_endless(progress.delivery(d).chain) ? 0 : progress.delivery(d).points;
        assert(finite < spent && finite * 2 > spent);
        std::size_t rounds = 0;
        for (; progress.bought_count() < all_techs().size(); ++rounds) {
            assert(rounds < 100);
            const std::vector<std::size_t> open = progress.open_deliveries();
            assert(!open.empty());  // never stuck: a standing order, at least
            for (const std::size_t d : open)
                assert(progress.complete(d));
            std::optional<std::size_t> cheapest;
            for (std::size_t t = 0; t < all_techs().size(); ++t)
                if (progress.can_buy(t) && (!cheapest || all_techs()[t].cost < all_techs()[*cheapest].cost))
                    cheapest = t;
            if (rounds == 0)
                assert(cheapest.has_value());  // the first tech, from the first deliveries
            if (cheapest)
                assert(progress.buy(*cheapest));
        }
        assert(progress.earned() - spent == progress.points());
        // Every finite delivery can be made now.
        while (true) {
            const std::vector<std::size_t> open = progress.open_deliveries();
            const auto finite_open = std::find_if(open.begin(), open.end(), [&](std::size_t d) {
                return !chain_endless(progress.delivery(d).chain);
            });
            if (finite_open == open.end())
                break;
            assert(progress.complete(*finite_open));
        }
        for (int chain = 0; chain < kDeliveryChains; ++chain)
            assert(chain_endless(chain) ? progress.chain_current(chain).has_value()
                                        : !progress.chain_current(chain).has_value());
    }
}

// Open deliveries, one per chain at a time, delivering (capped at what's
// wanted, and only toward open ones), completing one paying out and
// opening the next in its chain, and the shop: techs for sale once those
// they come after are bought, bought only with the points for them.
void test_progress() {
    Progress progress;
    // The electronics works: its second wants steel.
    constexpr int kWorks = 1;
    assert(!chain_endless(kWorks));
    const std::vector<std::size_t> works = progress.chain_deliveries(kWorks);
    const std::size_t first = works[0];
    const std::size_t second = works[1];
    const std::size_t steelworks = tech_index("Steelworks");
    const std::size_t refining = tech_index("Refining");
    assert(progress.delivery(second).wants.front().kind == ItemKind::Steel);
    // Each chain's first is open, except those asking for what needs a
    // tech; the rest not.
    for (int chain = 0; chain < kDeliveryChains; ++chain) {
        const std::vector<std::size_t> in_chain = progress.chain_deliveries(chain);
        assert(progress.open(in_chain.front()) == (tier_needed(progress.delivery(in_chain.front()).wants) == 0));
        assert(progress.chain_current(chain) == in_chain.front());
        for (std::size_t i = 1; i < in_chain.size(); ++i)
            assert(!progress.open(in_chain[i]));
    }
    assert(progress.open_deliveries().size() >= 3);
    assert(!progress.unlocked(ItemKind::Steel) && progress.unlocked(ItemKind::FuelCell));
    assert(progress.done_count() == 0 && progress.points() == 0 && progress.earned() == 0);

    // Nothing toward one that isn't open, or of what it doesn't want.
    assert(!progress.deliver(second, ItemKind::Steel) && progress.delivered(second, ItemKind::Steel) == 0);
    assert(progress.wanted(second, ItemKind::Steel) == 0);
    assert(!progress.deliver(first, ItemKind::Steel) && progress.delivered(first, ItemKind::Steel) == 0);

    const std::vector<ItemStack> wants = progress.delivery(first).wants;
    const int pays = progress.delivery(first).points;
    // Past what's wanted doesn't count.
    assert(!progress.deliver(first, wants[0].kind, wants[0].count + 10));
    assert(progress.delivered(first, wants[0].kind) == wants[0].count);
    assert(progress.wanted(first, wants[0].kind) == 0);
    for (std::size_t i = 1; i < wants.size(); ++i) {
        assert(!progress.deliver(first, wants[i].kind, wants[i].count - 1));
        assert(progress.wanted(first, wants[i].kind) == 1);
    }
    assert(progress.points() == 0);
    assert(progress.deliver(first, wants.back().kind));  // the last one in
    assert(progress.done(first) && !progress.open(first) && progress.done_count() == 1);
    assert(progress.points() == pays && progress.earned() == pays);
    assert(!progress.deliver(first, wants[0].kind));  // done is done
    assert(progress.points() == pays);
    // A finite chain makes up no more.
    assert(progress.chain_deliveries(kWorks).size() == static_cast<std::size_t>(kFiniteChainLength));

    // The next in the chain asks for steel: it waits for the tech.
    assert(progress.chain_current(kWorks) == second && !progress.open(second));
    const std::vector<std::size_t> opened = progress.deliveries_opened_by(steelworks);
    assert(std::find(opened.begin(), opened.end(), second) != opened.end());
    assert(std::find(opened.begin(), opened.end(), first) == opened.end());

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
    assert(progress.open(second));
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

    for (std::size_t t = 0; t < all_techs().size(); ++t)
        progress.grant(t);
    assert(progress.bought_count() == all_techs().size());
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
    // A finite chain's first: two items, neither steel.
    const std::size_t first = progress.chain_deliveries(1).front();
    const Delivery delivery = progress.delivery(first);
    assert(delivery.wants.size() == 2);
    const ItemKind plate = delivery.wants[0].kind;
    const ItemKind gear = delivery.wants[1].kind;
    assert(plate != ItemKind::Steel && gear != ItemKind::Steel);
    const int sector = world.get_map().delivery_sector(delivery);
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
    const int plates = ship.inventory().count(plate);
    for (Tick t = 0; t < kTicksPerSecond; ++t) {
        top_up(provider, plate);
        world.advance_tick();
    }
    assert(!provider.accepts(plate) && provider.contents().empty());
    assert(progress.delivered(first, plate) == 0);
    assert(ship.inventory().count(plate) == plates);

    // In the sector that takes it: an item every kLinkTicks, used up.
    flagship.sector = sector;
    world.advance_tick();
    const Tick run = 20 * kLinkTicks;
    for (Tick t = 0; t < run; ++t) {
        top_up(provider, plate);
        world.advance_tick();
        assert(provider.contents().empty());  // passed on within the tick
    }
    const int went = progress.delivered(first, plate);
    assert(went >= 19 && went <= 21);

    // Here, it doesn't take what the delivery doesn't want.
    idle();
    assert(!provider.accepts(ItemKind::Steel) && provider.accepts(gear));

    // Once the iron plates are all in, it takes no more of them: a surplus
    // can't keep the gears out.
    Progress& shared = world.get_fleet().get_progress();
    shared.deliver(first, plate, progress.wanted(first, plate));
    world.advance_tick();
    assert(!provider.accepts(plate) && provider.accepts(gear));

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
    assert(provider.accepts(ItemKind::Steel) && !provider.accepts(plate));
    // Away from any delivery, only that.
    flagship.sector = 0;
    world.advance_tick();
    assert(provider.accepts(ItemKind::Steel) && !provider.accepts(gear) &&
           !provider.accepts(plate));
    flagship.sector = sector;
    assert(ship.remove_building(req) == BuildResult::Ok);
    world.advance_tick();

    // All but the last gear in; the provider's last gear completes it, the
    // tick it takes it.
    shared.deliver(first, gear, progress.wanted(first, gear) - 1);
    world.advance_tick();
    assert(!progress.done(first));
    world.take_events();
    assert(provider.accepts(gear));
    provider.receive(gear);
    world.advance_tick();
    assert(progress.done(first) && progress.points() == delivery.points);
    const std::vector<Event> events = world.take_events();
    const auto done = std::count_if(events.begin(), events.end(), [&](const Event& e) {
        return e.kind == EventKind::DeliveryComplete && e.delivery == static_cast<int>(first) && e.sector == sector &&
               e.ship == flagship.id;
    });
    assert(done == 1);
    // Then nothing more is wanted here: it takes no more gears.
    idle();
    assert(!provider.accepts(gear));

    // An item it was fed when nothing asked for it (between ticks, here)
    // goes to its bridge, not lost.
    const int gears = ship.inventory().count(gear);
    provider.receive(gear);
    world.advance_tick();
    assert(provider.contents().empty() && ship.inventory().count(gear) == gears + 1);
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
        const Progress progress(seed);
        for (std::size_t d = 0; d < progress.delivery_count(); ++d) {
            const int s = map.delivery_sector(progress.delivery(d));
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
                assert((map.delivery_sector(progress.delivery(e)) == s) ==
                       (progress.delivery(e).chain == progress.delivery(d).chain));
            const std::vector<std::size_t> here = map.deliveries_at(s, progress);
            assert(std::find(here.begin(), here.end(), d) != here.end());
        }
        assert(map.linked(0, map.chain_sector(0)));
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
    test_themes_and_techs();
    test_made_up();
    test_no_dead_ends();
    test_balance();
    test_endless();
    test_economy();
    test_progress();
    test_provider_delivers();
    test_map_placement();
    test_travel_from_the_start();
    std::puts("test_deliveries: all passed");
    return 0;
}
