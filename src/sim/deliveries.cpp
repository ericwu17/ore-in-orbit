// deliveries.cpp — the delivery table and progress through it (see
// deliveries.h).
#include "sim/deliveries.h"

#include <algorithm>
#include <cassert>

const std::vector<Delivery>& all_deliveries() {
    using enum ItemKind;
    // Travel needs nothing from here: fuel cells and providers are
    // unlocked from the start, so the first delivery is a jump away from
    // the first minutes. Each tier asks for more than the one before it
    // (in raw resources: see test_deliveries), and for items further up
    // the chain. Two branches open at tiers 2 and 5, for the player to
    // pick between, and join up again after.
    static const std::vector<Delivery> deliveries = {
        // Tier 1: plates and gears, from what a new ship can make.
        {"First light",
         {},
         {{IronPlate, 80}, {CopperPlate, 40}, {Gear, 40}},
         {Steel, FilterSorter, ReverseSorter, Router, Unloader}},
        // Tier 2: two branches, needing steel.
        {"Heavy industry",
         {"First light"},
         {{Steel, 40}, {Circuit, 80}},
         {Refinery, Plastic, Silicon, AdvancedCircuit, StructuralFrame}},
        {"Supply lines",
         {"First light"},
         {{Steel, 30}, {Gear, 80}, {Glass, 40}},
         {ExpressBelt, BeltTunnel, CargoPod}},
        // Tier 3: the second ship, and bulk logistics.
        {"Deep space",
         {"Heavy industry", "Supply lines"},
         {{AdvancedCircuit, 50}, {StructuralFrame, 25}, {FuelCell, 40}},
         {FabricatorMk2, TractorArray, ShipStarterPack, Requester}},
        // Tier 4: weapons, and iridium (a tractor array, from Deep space,
        // gathers it).
        {"Escape velocity",
         {"Deep space"},
         {{AdvancedCircuit, 60}, {StructuralFrame, 30}, {Glass, 100}},
         {FocusCrystal, IridiumPlate}},
        // Tier 5: two branches again.
        {"Frontier",
         {"Escape velocity"},
         {{FuelCell, 150}, {FocusCrystal, 30}, {Steel, 60}},
         {ShieldProjector}},
        {"Iridium rush",
         {"Escape velocity"},
         {{IridiumPlate, 150}, {AdvancedCircuit, 100}},
         {Processor}},
        // Tier 6: the capital ship's fabricator.
        {"Starforge",
         {"Frontier", "Iridium rush"},
         {{Processor, 50}, {IridiumPlate, 100}, {StructuralFrame, 40}},
         {FabricatorMk3}},
    };
    return deliveries;
}

std::size_t delivery_index(std::string_view name) {
    const std::vector<Delivery>& deliveries = all_deliveries();
    for (std::size_t i = 0; i < deliveries.size(); ++i)
        if (deliveries[i].name == name)
            return i;
    assert(false && "no delivery by that name");
    return deliveries.size();
}

std::vector<std::size_t> delivery_prerequisites(std::size_t d) {
    std::vector<std::size_t> before;
    for (const std::string_view name : all_deliveries()[d].after)
        before.push_back(delivery_index(name));
    return before;
}

int delivery_tier(std::size_t d) {
    int tier = 1;
    for (const std::size_t p : delivery_prerequisites(d))
        tier = std::max(tier, delivery_tier(p) + 1);
    return tier;
}

int max_delivery_tier() {
    int tier = 0;
    for (std::size_t d = 0; d < all_deliveries().size(); ++d)
        tier = std::max(tier, delivery_tier(d));
    return tier;
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

std::optional<std::size_t> delivery_unlocking(ItemKind kind) {
    const std::vector<Delivery>& deliveries = all_deliveries();
    for (std::size_t i = 0; i < deliveries.size(); ++i) {
        const std::vector<ItemKind>& unlocks = deliveries[i].unlocks;
        if (std::find(unlocks.begin(), unlocks.end(), kind) != unlocks.end())
            return i;
    }
    return std::nullopt;
}

Progress::Progress()
    : complete_(all_deliveries().size(), false), delivered_(all_deliveries().size(), std::array<int, kItemKindCount>{}) {}

bool Progress::open(std::size_t d) const {
    if (d >= complete_.size() || complete_[d])
        return false;
    const std::vector<std::size_t> before = delivery_prerequisites(d);
    return std::all_of(before.begin(), before.end(), [&](std::size_t p) { return complete_[p]; });
}

std::vector<std::size_t> Progress::open_deliveries() const {
    std::vector<std::size_t> open_now;
    for (std::size_t d = 0; d < complete_.size(); ++d)
        if (open(d))
            open_now.push_back(d);
    return open_now;
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
    return true;
}

bool Progress::complete(std::size_t d) {
    if (!open(d))
        return false;
    for (const ItemStack& s : all_deliveries()[d].wants)
        delivered_[d][static_cast<std::size_t>(s.kind)] = s.count;
    complete_[d] = true;
    return true;
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
    const std::optional<std::size_t> at = delivery_unlocking(kind);
    return !at || done(*at);
}
