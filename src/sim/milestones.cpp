// milestones.cpp — the milestone table and progress through it (see
// milestones.h).
#include "sim/milestones.h"

#include <algorithm>
#include <cassert>

const std::vector<Milestone>& all_milestones() {
    using enum ItemKind;
    static const std::vector<Milestone> milestones = {
        {"First light",
         {{IronPlate, 50}, {CopperPlate, 20}},
         {FilterSorter, ReverseSorter, Router, Unloader, Steel}},
        {"Heavy industry",
         {{Circuit, 100}, {Steel, 50}},
         {Refinery, Plastic, Silicon, AdvancedCircuit, StructuralFrame, ExpressBelt, BeltTunnel}},
        {"Deep space",
         {{AdvancedCircuit, 50}, {StructuralFrame, 25}},
         {FabricatorMk2, TractorArray, SurveyScanner, ShipStarterPack, CargoPod}},
        // Travel and weapons, from what home has: iridium is only to be had
        // in other sectors, so the milestones that need it come after.
        {"Escape velocity",
         {{AdvancedCircuit, 40}, {StructuralFrame, 15}, {Glass, 60}},
         {FuelCell, FocusCrystal, IridiumPlate, Processor}},
        {"Frontier",
         {{FuelCell, 100}, {FocusCrystal, 30}},
         {ShieldProjector}},
        {"Starforge",
         {{IridiumPlate, 20}, {Processor, 20}},
         {FabricatorMk3}},
    };
    return milestones;
}

std::size_t milestone_index(std::string_view name) {
    const std::vector<Milestone>& milestones = all_milestones();
    for (std::size_t i = 0; i < milestones.size(); ++i)
        if (milestones[i].name == name)
            return i;
    assert(false && "no milestone by that name");
    return milestones.size();
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

std::optional<std::size_t> milestone_unlocking(ItemKind kind) {
    const std::vector<Milestone>& milestones = all_milestones();
    for (std::size_t i = 0; i < milestones.size(); ++i) {
        const std::vector<ItemKind>& unlocks = milestones[i].unlocks;
        if (std::find(unlocks.begin(), unlocks.end(), kind) != unlocks.end())
            return i;
    }
    return std::nullopt;
}

const Milestone* Progress::current_milestone() const {
    const std::vector<Milestone>& milestones = all_milestones();
    return current < milestones.size() ? &milestones[current] : nullptr;
}

bool Progress::on_delivered(ItemKind kind) {
    const Milestone* m = current_milestone();
    if (!m)
        return false;
    bool done = true;
    for (const ItemStack& s : m->deliver) {
        int& have = delivered[static_cast<std::size_t>(s.kind)];
        if (s.kind == kind)
            ++have;
        done = done && have >= s.count;
    }
    return done && complete_current();
}

void Progress::on_unloaded(ItemKind kind) {
    const Milestone* m = current_milestone();
    if (!m)
        return;
    for (const ItemStack& s : m->deliver)
        if (s.kind == kind)
            --delivered[static_cast<std::size_t>(kind)];
}

bool Progress::complete_current() {
    if (all_complete())
        return false;
    ++current;
    delivered = {};
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
    const std::optional<std::size_t> at = milestone_unlocking(kind);
    return !at || *at < current;
}
