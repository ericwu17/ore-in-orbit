// deliveries.cpp — the delivery and tech tables, and progress through them
// (see deliveries.h).
#include "sim/deliveries.h"

#include <algorithm>
#include <cassert>
#include <cmath>

const std::vector<Delivery>& all_deliveries() {
    using enum ItemKind;
    // Five chains, one per sector, nearest home first. Each starts with
    // what a new ship can make (plates, gears, circuits, glass, fuel cells;
    // the last chain with steel), and climbs from there; a delivery opens
    // once the techs for everything it asks for are bought.
    static const std::vector<Delivery> deliveries = {
        // Chain 0: a shipyard.
        {"Hull plating", 0, {{IronPlate, 60}, {Gear, 30}}},
        {"Girders", 0, {{Steel, 40}, {Gear, 40}}},
        {"Frame order", 0, {{StructuralFrame, 30}, {Steel, 40}}},
        {"Armored keel", 0, {{StructuralFrame, 60}, {IridiumPlate, 60}}},
        // Chain 1: an electronics works.
        {"Wiring loom", 1, {{CopperPlate, 40}, {Circuit, 40}}},
        {"Control boards", 1, {{Circuit, 100}, {Glass, 30}}},
        {"Guidance cores", 1, {{AdvancedCircuit, 50}, {Plastic, 30}}},
        {"Mainframe", 1, {{Processor, 30}, {AdvancedCircuit, 40}}},
        // Chain 2: a fuel depot.
        {"Fuel run", 2, {{FuelCell, 40}, {IronPlate, 40}}},
        {"Tanker contract", 2, {{FuelCell, 80}, {Steel, 20}}},
        {"Reactor feed", 2, {{FuelCell, 120}, {Silicon, 40}, {Steel, 20}}},
        {"Picket resupply", 2, {{FuelCell, 150}, {FocusCrystal, 30}}},
        // Chain 3: an observatory.
        {"Lens blanks", 3, {{Glass, 50}, {CopperPlate, 30}}},
        {"Telescope mounts", 3, {{Glass, 80}, {Steel, 30}}},
        {"Sensor array", 3, {{Silicon, 80}, {Glass, 100}, {AdvancedCircuit, 20}}},
        {"Deep-field optics", 3, {{FocusCrystal, 40}, {Glass, 120}}},
        // Chain 4: a frontier outpost, the furthest out.
        {"Outpost kit", 4, {{Steel, 30}, {Circuit, 50}}},
        {"Habitat ring", 4, {{StructuralFrame, 20}, {AdvancedCircuit, 20}}},
        {"Iridium bulkheads", 4, {{IridiumPlate, 100}, {StructuralFrame, 40}}},
        {"Command spire", 4, {{Processor, 40}, {IridiumPlate, 80}, {FocusCrystal, 20}}},
    };
    return deliveries;
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
        // Logistics first, so a second ship can be sent cargo pods (and the fuel
        // in them) from the start, rather than sitting empty.
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

std::size_t delivery_index(std::string_view name) {
    return index_by_name(all_deliveries(), name);
}

std::size_t tech_index(std::string_view name) {
    return index_by_name(all_techs(), name);
}

std::vector<std::size_t> chain_deliveries(int chain) {
    std::vector<std::size_t> in_chain;
    for (std::size_t d = 0; d < all_deliveries().size(); ++d)
        if (all_deliveries()[d].chain == chain)
            in_chain.push_back(d);
    return in_chain;
}

std::optional<std::size_t> delivery_before(std::size_t d) {
    std::optional<std::size_t> before;
    for (std::size_t e = 0; e < d; ++e)
        if (all_deliveries()[e].chain == all_deliveries()[d].chain)
            before = e;
    return before;
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

int delivery_points(std::size_t d) {
    double raw = 0.0;
    for (const ItemStack& s : all_deliveries()[d].wants)
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

std::vector<std::size_t> deliveries_opened_by(std::size_t t) {
    std::vector<std::size_t> opened;
    for (std::size_t d = 0; d < all_deliveries().size(); ++d)
        for (const ItemStack& want : all_deliveries()[d].wants)
            if (tech_unlocking(want.kind) == t) {
                opened.push_back(d);
                break;
            }
    return opened;
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

Progress::Progress()
    : complete_(all_deliveries().size(), false),
      delivered_(all_deliveries().size(), std::array<int, kItemKindCount>{}),
      bought_(all_techs().size(), false) {}

bool Progress::open(std::size_t d) const {
    if (d >= complete_.size() || complete_[d])
        return false;
    if (const std::optional<std::size_t> before = delivery_before(d); before && !complete_[*before])
        return false;
    const std::vector<ItemStack>& wants = all_deliveries()[d].wants;
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
    for (const std::size_t d : chain_deliveries(chain))
        if (!complete_[d])
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
    for (const ItemStack& s : all_deliveries()[d].wants)
        if (s.kind == kind)
            return std::max(0, s.count - delivered(d, kind));
    return 0;
}

bool Progress::deliver(std::size_t d, ItemKind kind, int count) {
    const int take = std::min(count, wanted(d, kind));
    if (take <= 0)
        return false;
    delivered_[d][static_cast<std::size_t>(kind)] += take;
    const std::vector<ItemStack>& wants = all_deliveries()[d].wants;
    const bool all_in = std::all_of(wants.begin(), wants.end(), [&](const ItemStack& s) {
        return delivered(d, s.kind) >= s.count;
    });
    if (!all_in)
        return false;
    complete_[d] = true;
    points_ += delivery_points(d);
    return true;
}

bool Progress::complete(std::size_t d) {
    if (!open(d))
        return false;
    for (const ItemStack& s : all_deliveries()[d].wants)
        delivered_[d][static_cast<std::size_t>(s.kind)] = s.count;
    complete_[d] = true;
    points_ += delivery_points(d);
    return true;
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
