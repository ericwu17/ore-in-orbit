// deliveries.cpp — the delivery and tech tables, and progress through them
// (see deliveries.h).
#include "sim/deliveries.h"

#include "sim/rng.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace {

// One step of a finite chain: what its main good is picked from (each of
// the same tech tier, so a step never needs a lower tier than the one
// before it), and what goes with it (no higher tier).
struct ChainStep {
    std::vector<ItemKind> main;
    std::vector<ItemKind> side;
};

// A sector's line of goods. A finite chain's steps; an endless one picks
// from `items`, whatever of them the player can make by then.
struct ChainTheme {
    const char* role;
    bool endless;
    std::vector<ItemKind> items;          // endless: everything it may ask for
    std::vector<ChainStep> steps;         // finite: kFiniteChainLength of them
    std::vector<const char*> nouns;       // to name its deliveries by
};

const std::vector<ChainTheme>& themes() {
    using enum ItemKind;
    // Five chains, one per sector, nearest home first. The nearest, a
    // shipyard, and the fuel depot keep ordering for good; the rest have
    // four orders each, the first of what a new ship can make (plates,
    // gears, circuits, glass; the furthest, the frontier outpost's, steel,
    // so it's out of sight till then), climbing from there.
    static const std::vector<ChainTheme> table = {
        {"Shipyard", true, {IronPlate, Gear, Steel, StructuralFrame, IridiumPlate}, {},
         {"hull order", "keel contract", "dock requisition"}},
        {"Electronics works", false, {},
         {
             {{Circuit}, {CopperPlate, IronPlate}},
             {{Steel}, {Circuit, Glass}},
             {{AdvancedCircuit}, {Circuit, Plastic, Silicon}},
             {{Processor}, {AdvancedCircuit, Plastic}},
         },
         {"wiring order", "board contract", "component run"}},
        {"Fuel depot", true, {FuelCell, IronPlate, Steel, Silicon, FocusCrystal}, {},
         {"fuel run", "tanker contract", "reactor feed"}},
        {"Observatory", false, {},
         {
             {{Glass}, {CopperPlate, IronPlate}},
             {{Steel, FocusCrystal}, {Glass, CopperPlate}},
             {{AdvancedCircuit}, {Glass, Silicon}},
             {{IridiumPlate}, {Glass, FocusCrystal, AdvancedCircuit}},
         },
         {"lens order", "mirror contract", "instrument run"}},
        {"Frontier outpost", false, {},
         {
             {{Steel}, {Circuit, Gear}},
             {{StructuralFrame}, {Steel, Circuit}},
             {{AdvancedCircuit}, {StructuralFrame, Plastic}},
             {{Processor}, {IridiumPlate, StructuralFrame}},
         },
         {"supply drop", "habitat contract", "outpost kit"}},
    };
    return table;
}

// The tech tier `kind` needs: 0 when unlocked from the start.
int item_tier(ItemKind kind) {
    const std::optional<std::size_t> t = tech_unlocking(kind);
    return t ? tech_tier(*t) : 0;
}

// A random number stream for step `step` of chain `chain`, from `seed`.
Rng step_rng(std::uint64_t seed, int chain, int step) {
    const std::uint64_t c = static_cast<unsigned>(chain);
    const std::uint64_t n = static_cast<unsigned>(step);
    Rng mix{seed ^ (c * 0x9E3779B97F4A7C15ull) ^ (n * 0xC2B2AE3D27D4EB4Full)};
    return Rng{mix.next()};
}

template <typename T>
const T& pick(Rng& rng, const std::vector<T>& from) {
    return from[static_cast<std::size_t>(rng.range(0, static_cast<int>(from.size()) - 1))];
}

// Goods taking about `raw` raw resources: `main` the most of it (three
// fifths to three quarters), `side` (if any) the rest, each rounded to a
// whole five (at least five).
std::vector<ItemStack> goods(Rng& rng, double raw, ItemKind main, std::optional<ItemKind> side) {
    const double share = side ? 0.6 + 0.15 * static_cast<double>(rng.unit()) : 1.0;
    const auto count = [](ItemKind kind, double part) {
        const double per = std::max(1.0, raw_resources_in(kind, 1.0));
        return std::max(5, static_cast<int>(std::lround(part / per / 5.0)) * 5);
    };
    std::vector<ItemStack> wants = {{main, count(main, raw * share)}};
    if (side)
        wants.push_back({*side, count(*side, raw * (1.0 - share))});
    return wants;
}

Delivery make_delivery(const ChainTheme& theme, Rng& rng, int chain, int step, std::vector<ItemStack> wants) {
    std::string name = item_name(wants.front().kind);
    name += std::string(" ") + pick(rng, theme.nouns);
    if (theme.endless)
        name += " #" + std::to_string(step + 1);
    const int points = delivery_points(wants);
    return {std::move(name), chain, std::move(wants), points};
}

// Step `step` of finite chain `chain`.
Delivery finite_delivery(std::uint64_t seed, int chain, int step) {
    const ChainTheme& theme = themes()[static_cast<std::size_t>(chain)];
    const ChainStep& at = theme.steps[static_cast<std::size_t>(step)];
    Rng rng = step_rng(seed, chain, step);
    const ItemKind main = pick(rng, at.main);
    std::vector<ItemKind> sides;
    for (const ItemKind k : at.side)
        if (k != main)
            sides.push_back(k);
    const std::optional<ItemKind> side = sides.empty() ? std::nullopt : std::optional(pick(rng, sides));
    const double raw = kFiniteStepPoints[static_cast<std::size_t>(step)] * kRawPerPoint;
    return make_delivery(theme, rng, chain, step, goods(rng, raw, main, side));
}

// Step `step` of endless chain `chain`, from what `unlocked` says can be
// made, sized for `earned` points earned so far. Its main good is one of
// the two most advanced the player can make (highest tier, then most raw
// resources a piece), and anything else unlocked goes with it.
template <typename Unlocked>
Delivery endless_delivery(std::uint64_t seed, int chain, int step, long long earned, Unlocked unlocked) {
    const ChainTheme& theme = themes()[static_cast<std::size_t>(chain)];
    Rng rng = step_rng(seed, chain, step);
    std::vector<ItemKind> can;
    for (const ItemKind k : theme.items)
        if (unlocked(k))
            can.push_back(k);
    assert(!can.empty() && "an endless chain asks for something made from the start");
    std::stable_sort(can.begin(), can.end(), [](ItemKind a, ItemKind b) {
        if (item_tier(a) != item_tier(b))
            return item_tier(a) > item_tier(b);
        return raw_resources_in(a, 1.0) > raw_resources_in(b, 1.0);
    });
    const std::size_t top = std::min<std::size_t>(2, can.size());
    const std::size_t m = static_cast<std::size_t>(rng.range(0, static_cast<int>(top) - 1));
    const ItemKind main = can[m];
    can.erase(can.begin() + static_cast<std::ptrdiff_t>(m));
    const std::optional<ItemKind> side = can.empty() ? std::nullopt : std::optional(pick(rng, can));
    const double raw = endless_target_points(earned) * kRawPerPoint;
    return make_delivery(theme, rng, chain, step, goods(rng, raw, main, side));
}

}  // namespace

bool chain_endless(int chain) {
    return themes()[static_cast<std::size_t>(chain)].endless;
}

const char* chain_role(int chain) {
    return themes()[static_cast<std::size_t>(chain)].role;
}

std::vector<ItemKind> chain_items(int chain) {
    const ChainTheme& theme = themes()[static_cast<std::size_t>(chain)];
    std::vector<ItemKind> items = theme.items;
    for (const ChainStep& step : theme.steps)
        for (const std::vector<ItemKind>* list : {&step.main, &step.side})
            for (const ItemKind k : *list)
                if (std::find(items.begin(), items.end(), k) == items.end())
                    items.push_back(k);
    return items;
}

int endless_target_points(long long earned) {
    return kEndlessBasePoints + static_cast<int>(std::lround(kEndlessEarnedShare * static_cast<double>(std::max(0LL, earned))));
}

const std::vector<Tech>& all_techs() {
    using enum ItemKind;
    // A wide tree of cheap techs, so there's a choice at every step: four
    // to pick from at the start, each opening more. Travel needs none of
    // them: fuel cells and providers are there from the start. Steel and
    // refining lead on to most of the rest; the ships, fabricators and
    // combat techs branch off them, and join up again in the capstones.
    static const std::vector<Tech> techs = {
        // Tier 1: from the start.
        {"Steelworks", 8, {}, {Steel}, "Steel, in a solar furnace: the way to bigger deliveries"},
        {"Sorting", 5, {}, {FilterSorter, ReverseSorter, Router}, "Split and merge belts"},
        {"Unloading", 5, {}, {Unloader, BeltTunnel},
         "Pull items out of the bridge and other buildings, and tunnel belts under others"},
        {"Optics", 8, {}, {FocusCrystal}, "Focus crystals: the burst laser's ammunition, and two late deliveries"},
        // Tier 2.
        {"Refining", 15, {"Steelworks"}, {Refinery, Plastic, Silicon}, "The refinery, for plastic and silicon"},
        {"Structures", 12, {"Steelworks"}, {StructuralFrame}, "Structural frames, from steel and gears"},
        {"Logistics", 12, {"Steelworks", "Sorting"}, {ExpressBelt, CargoPod},
         "Express belts, twice as fast; cargo pods, to send cargo between ships"},
        // Tier 3.
        {"Microelectronics", 20, {"Refining"}, {AdvancedCircuit}, "Advanced circuits, for nearly all that follows"},
        // Tier 4.
        {"Shipwright", 30, {"Structures", "Microelectronics", "Logistics"}, {ShipStarterPack},
         "Ship starter packs, to build a second ship, and the Hauler"},
        {"Fabrication", 25, {"Structures", "Microelectronics"}, {FabricatorMk2}, "The Fabricator Mk2: faster assembly"},
        {"Tractor beams", 15, {"Structures", "Microelectronics"}, {TractorArray},
         "The tractor array: a faster collector, for every resource"},
        {"Requesters", 20, {"Logistics", "Microelectronics"}, {Requester},
         "Requesters, to pull items from providers on other ships"},
        {"Shielding", 25, {"Structures", "Microelectronics"}, {ShieldProjector}, "Shield projectors, for fights"},
        // Tier 5: iridium (which only tractor arrays gather), the Frigate,
        // and what they lead to.
        {"Iridium refining", 15, {"Tractor beams"}, {IridiumPlate}, "Iridium plates, from the ore tractor arrays gather"},
        {"Frigate hull", 35, {"Shipwright", "Shielding"}, {}, "The Frigate, a bigger warship"},
        // Tier 6.
        {"Processors", 25, {"Microelectronics", "Iridium refining"}, {Processor},
         "Processors, for the last deliveries"},
        // Tier 7: the capstones.
        {"Starforge", 50, {"Fabrication", "Processors"}, {FabricatorMk3}, "The Fabricator Mk3: the fastest assembly"},
        {"Carrier hull", 50, {"Frigate hull", "Processors"}, {}, "The Carrier, the biggest ship"},
    };
    return techs;
}

namespace {

template <typename T>
std::size_t index_by_name(const std::vector<T>& table, std::string_view name) {
    for (std::size_t i = 0; i < table.size(); ++i)
        if (table[i].name == name)
            return i;
    assert(false && "nothing by that name");
    return table.size();
}

}  // namespace

std::size_t tech_index(std::string_view name) {
    return index_by_name(all_techs(), name);
}

double raw_resources_in(ItemKind kind, double count) {
    if (item_category(kind) == ItemCategory::RawResource)
        return count;
    const Recipe* recipe = recipe_for(kind);
    if (!recipe)
        return 0.0;
    const double runs = count / recipe->outputs.front().count;
    double total = 0.0;
    for (const ItemStack& in : recipe->inputs)
        total += raw_resources_in(in.kind, runs * in.count);
    return total;
}

int delivery_points(const std::vector<ItemStack>& wants) {
    double raw = 0.0;
    for (const ItemStack& s : wants)
        raw += raw_resources_in(s.kind, s.count);
    return std::max(1, static_cast<int>(std::lround(raw / kRawPerPoint)));
}

std::vector<std::size_t> tech_prerequisites(std::size_t t) {
    std::vector<std::size_t> before;
    for (const std::string_view name : all_techs()[t].after)
        before.push_back(tech_index(name));
    return before;
}

std::optional<ItemKind> weapon_unlocking(ItemKind kind) {
    using enum ItemKind;
    switch (kind) {
    case Missile: return MissileLauncher;
    case IonCell: return IonBlaster;
    case FlakShell: return FlakCannon;
    case IridiumLens: return BeamEmitter;
    default: return std::nullopt;
    }
}

int tech_tier(std::size_t t) {
    int tier = 1;
    for (const std::size_t p : tech_prerequisites(t))
        tier = std::max(tier, tech_tier(p) + 1);
    return tier;
}

std::optional<std::size_t> tech_unlocking(ItemKind kind) {
    const std::vector<Tech>& techs = all_techs();
    for (std::size_t i = 0; i < techs.size(); ++i) {
        const std::vector<ItemKind>& unlocks = techs[i].unlocks;
        if (std::find(unlocks.begin(), unlocks.end(), kind) != unlocks.end())
            return i;
    }
    return std::nullopt;
}

Progress::Progress(std::uint64_t seed) : bought_(all_techs().size(), false) {
    make_up_deliveries(seed);
}

void Progress::make_up_deliveries(std::uint64_t seed) {
    seed_ = seed;
    deliveries_.clear();
    complete_.clear();
    delivered_.clear();
    for (int chain = 0; chain < kDeliveryChains; ++chain)
        if (!chain_endless(chain))
            for (int step = 0; step < kFiniteChainLength; ++step) {
                deliveries_.push_back(finite_delivery(seed_, chain, step));
                complete_.push_back(false);
                delivered_.push_back({});
            }
    extend_endless();
}

void Progress::extend_endless() {
    for (int chain = 0; chain < kDeliveryChains; ++chain) {
        if (!chain_endless(chain) || chain_current(chain))
            continue;
        const int step = static_cast<int>(chain_deliveries(chain).size());
        deliveries_.push_back(
            endless_delivery(seed_, chain, step, earned_, [this](ItemKind k) { return unlocked(k); }));
        complete_.push_back(false);
        delivered_.push_back({});
    }
}

void Progress::finish(std::size_t d) {
    complete_[d] = true;
    points_ += deliveries_[d].points;
    earned_ += deliveries_[d].points;
    extend_endless();
}

std::vector<std::size_t> Progress::chain_deliveries(int chain) const {
    std::vector<std::size_t> in_chain;
    for (std::size_t d = 0; d < deliveries_.size(); ++d)
        if (deliveries_[d].chain == chain)
            in_chain.push_back(d);
    return in_chain;
}

std::optional<std::size_t> Progress::delivery_before(std::size_t d) const {
    std::optional<std::size_t> before;
    for (std::size_t e = 0; e < d; ++e)
        if (deliveries_[e].chain == deliveries_[d].chain)
            before = e;
    return before;
}

bool Progress::open(std::size_t d) const {
    if (d >= complete_.size() || complete_[d])
        return false;
    if (const std::optional<std::size_t> before = delivery_before(d); before && !complete_[*before])
        return false;
    const std::vector<ItemStack>& wants = deliveries_[d].wants;
    return std::all_of(wants.begin(), wants.end(), [&](const ItemStack& s) { return unlocked(s.kind); });
}

std::vector<std::size_t> Progress::open_deliveries() const {
    std::vector<std::size_t> open_now;
    for (std::size_t d = 0; d < complete_.size(); ++d)
        if (open(d))
            open_now.push_back(d);
    return open_now;
}

std::optional<std::size_t> Progress::chain_current(int chain) const {
    for (std::size_t d = 0; d < deliveries_.size(); ++d)
        if (deliveries_[d].chain == chain && !complete_[d])
            return d;
    return std::nullopt;
}

std::size_t Progress::done_count() const {
    return static_cast<std::size_t>(std::count(complete_.begin(), complete_.end(), true));
}

int Progress::delivered(std::size_t d, ItemKind kind) const {
    return d < delivered_.size() ? delivered_[d][static_cast<std::size_t>(kind)] : 0;
}

int Progress::wanted(std::size_t d, ItemKind kind) const {
    if (!open(d))
        return 0;
    for (const ItemStack& s : deliveries_[d].wants)
        if (s.kind == kind)
            return std::max(0, s.count - delivered(d, kind));
    return 0;
}

bool Progress::deliver(std::size_t d, ItemKind kind, int count) {
    const int take = std::min(count, wanted(d, kind));
    if (take <= 0)
        return false;
    delivered_[d][static_cast<std::size_t>(kind)] += take;
    const std::vector<ItemStack>& wants = deliveries_[d].wants;
    const bool all_in = std::all_of(wants.begin(), wants.end(), [&](const ItemStack& s) {
        return delivered(d, s.kind) >= s.count;
    });
    if (!all_in)
        return false;
    finish(d);
    return true;
}

bool Progress::complete(std::size_t d) {
    if (!open(d))
        return false;
    for (const ItemStack& s : deliveries_[d].wants)
        delivered_[d][static_cast<std::size_t>(s.kind)] = s.count;
    finish(d);
    return true;
}

std::vector<std::size_t> Progress::deliveries_opened_by(std::size_t t) const {
    std::vector<std::size_t> opened;
    for (std::size_t d = 0; d < deliveries_.size(); ++d) {
        if (complete_[d])
            continue;
        for (const ItemStack& want : deliveries_[d].wants)
            if (tech_unlocking(want.kind) == t) {
                opened.push_back(d);
                break;
            }
    }
    return opened;
}

bool Progress::for_sale(std::size_t t) const {
    if (t >= bought_.size() || bought_[t])
        return false;
    const std::vector<std::size_t> before = tech_prerequisites(t);
    return std::all_of(before.begin(), before.end(), [&](std::size_t p) { return bought_[p]; });
}

bool Progress::can_buy(std::size_t t) const {
    return for_sale(t) && points_ >= all_techs()[t].cost;
}

bool Progress::buy(std::size_t t) {
    if (!can_buy(t))
        return false;
    points_ -= all_techs()[t].cost;
    bought_[t] = true;
    return true;
}

bool Progress::grant(std::size_t t) {
    if (!for_sale(t))
        return false;
    bought_[t] = true;
    return true;
}

std::size_t Progress::bought_count() const {
    return static_cast<std::size_t>(std::count(bought_.begin(), bought_.end(), true));
}

bool Progress::has_found(ItemKind weapon) const {
    return std::find(found.begin(), found.end(), weapon) != found.end();
}

bool Progress::add_found(ItemKind weapon) {
    if (!is_weapon(weapon) || has_found(weapon))
        return false;
    found.push_back(weapon);
    return true;
}

bool Progress::unlocked(ItemKind kind) const {
    if (is_weapon(kind))
        return has_found(kind);
    if (const std::optional<ItemKind> weapon = weapon_unlocking(kind))
        return has_found(*weapon);
    const std::optional<std::size_t> at = tech_unlocking(kind);
    return !at || bought(*at);
}
