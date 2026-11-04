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
         {FabricatorMk2, TractorArray, SurveyScanner, IridiumPlate, Processor, ShipStarterPack,
          CargoPod}},
        {"Escape velocity",
         {{Processor, 20}},
         {FabricatorMk3, FuelCell, BurstLaser, FocusCrystal}},
        {"Frontier",
         {{FuelCell, 100}, {FocusCrystal, 100}},
         {ShieldProjector}},
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

const std::vector<Schematic>& all_schematics() {
    using enum ItemKind;
    static const std::vector<Schematic> schematics = {
        {HeavyLaser, {HeavyLaser}},  // fires the burst laser's focus crystals
        {MissileLauncher, {MissileLauncher, Missile}},
        {IonBlaster, {IonBlaster, IonCell}},
        {FlakCannon, {FlakCannon, FlakShell}},
        {BeamEmitter, {BeamEmitter, IridiumLens}},
    };
    return schematics;
}

const Schematic* schematic_unlocking(ItemKind kind) {
    for (const Schematic& s : all_schematics())
        if (std::find(s.unlocks.begin(), s.unlocks.end(), kind) != s.unlocks.end())
            return &s;
    return nullptr;
}

const Schematic* schematic_for(ItemKind weapon) {
    for (const Schematic& s : all_schematics())
        if (s.weapon == weapon)
            return &s;
    return nullptr;
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

bool Progress::has_schematic(ItemKind weapon) const {
    return std::find(schematics.begin(), schematics.end(), weapon) != schematics.end();
}

bool Progress::add_schematic(ItemKind weapon) {
    if (!schematic_for(weapon) || has_schematic(weapon))
        return false;
    schematics.push_back(weapon);
    return true;
}

bool Progress::unlocked(ItemKind kind) const {
    if (const Schematic* s = schematic_unlocking(kind))
        return has_schematic(s->weapon);
    const std::optional<std::size_t> at = milestone_unlocking(kind);
    return !at || *at < current;
}
