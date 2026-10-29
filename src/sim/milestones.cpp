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
         {FilterSorter, ReverseSorter, Router, SolarPanel, Steel}},
        {"Heavy industry",
         {{Circuit, 100}, {Steel, 50}},
         {Refinery, Plastic, Silicon, AdvancedCircuit, StructuralFrame, ExpressBelt, BeltTunnel, BatteryBank}},
        {"Deep space",
         {{AdvancedCircuit, 50}, {StructuralFrame, 25}},
         {FabricatorMk2, TractorArray, SurveyScanner, SolarArray, IridiumPlate, Processor, ShipStarterPack,
          CargoPod}},
        {"Escape velocity",
         {{Processor, 20}},
         {FabricatorMk3, FuelCell, Autocannon, SlugMagazine}},
        {"Frontier",
         {{FuelCell, 100}, {SlugMagazine, 200}},
         {Railgun, RailSlug}},
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
            have = std::min(have + 1, s.count);
        done = done && have >= s.count;
    }
    return done && complete_current();
}

bool Progress::complete_current() {
    if (all_complete())
        return false;
    ++current;
    delivered = {};
    return true;
}

bool Progress::unlocked(ItemKind kind) const {
    const std::optional<std::size_t> at = milestone_unlocking(kind);
    return !at || *at < current;
}
