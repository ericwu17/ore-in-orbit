// building.cpp — the inventory, building data (footprints, rotation,
// machine categories), the bridge's hand-crafting and the building factory
// (see building.h).
#include "sim/building.h"

#include <cassert>
#include <utility>

void Inventory::add(ItemKind kind, int n) {
    assert(n >= 0);
    counts[static_cast<std::size_t>(kind)] += n;
}

void Inventory::add(const std::vector<ItemStack>& stacks) {
    for (const ItemStack& s : stacks)
        add(s.kind, s.count);
}

bool Inventory::take(ItemKind kind, int n) {
    assert(n >= 0);
    int& have = counts[static_cast<std::size_t>(kind)];
    if (have < n)
        return false;
    have -= n;
    return true;
}

bool Inventory::has(const std::vector<ItemStack>& stacks) const {
    // Totals per kind, in case a kind is listed twice.
    std::array<int, kItemKindCount> need{};
    for (const ItemStack& s : stacks)
        need[static_cast<std::size_t>(s.kind)] += s.count;
    for (std::size_t i = 0; i < need.size(); ++i)
        if (need[i] > counts[i])
            return false;
    return true;
}

bool Inventory::take(const std::vector<ItemStack>& stacks) {
    if (!has(stacks))
        return false;
    for (const ItemStack& s : stacks)
        counts[static_cast<std::size_t>(s.kind)] -= s.count;
    return true;
}

GridSize footprint_of(ItemKind kind, Direction facing) {
    GridSize size{1, 1};
    switch (kind) {
    case ItemKind::Splitter:
    case ItemKind::FilterSorter:
        size = {2, 1};
        break;
    case ItemKind::CargoHold:
    case ItemKind::FluidTank:
    case ItemKind::AsteroidCollector:
    case ItemKind::HydrogenCollector:
    case ItemKind::AsteroidCrusher:
    case ItemKind::SolarFurnace:
    case ItemKind::IceMelter:
    case ItemKind::Electrolyzer:
    case ItemKind::SolarPanel:
    case ItemKind::FuelCellGenerator:
    case ItemKind::BatteryBank:
    case ItemKind::SurveyScanner:
        size = {2, 2};
        break;
    case ItemKind::TractorArray:
    case ItemKind::ArcFurnace:
    case ItemKind::ChemicalPlant:
    case ItemKind::FabricatorMk1:
    case ItemKind::FabricatorMk2:
    case ItemKind::FabricatorMk3:
    case ItemKind::SolarArray:
        size = {3, 3};
        break;
    case ItemKind::IonThruster:
        size = {2, 3};
        break;
    default:
        break;
    }
    if (is_rotatable(kind) && (facing == Direction::East || facing == Direction::West))
        std::swap(size.w, size.h);
    return size;
}

bool is_rotatable(ItemKind kind) {
    switch (kind) {
    case ItemKind::TransportBelt:
    case ItemKind::ExpressBelt:
    case ItemKind::BeltTunnel:
    case ItemKind::Splitter:
    case ItemKind::FilterSorter:
    case ItemKind::Pump:
        return true;
    default:
        return false;
    }
}

bool needs_reinforced_hull(ItemKind kind) {
    switch (kind) {
    case ItemKind::TractorArray:
    case ItemKind::ArcFurnace:
    case ItemKind::ChemicalPlant:
    case ItemKind::FabricatorMk2:
    case ItemKind::FabricatorMk3:
    case ItemKind::SolarArray:
        return true;
    default:
        return false;
    }
}

bool handles_fluid(ItemKind kind) {
    switch (kind) {
    case ItemKind::Pipe:
    case ItemKind::Pump:
    case ItemKind::FluidTank:
    case ItemKind::HydrogenCollector:
    case ItemKind::IceMelter:
    case ItemKind::Electrolyzer:
    case ItemKind::ChemicalPlant:
    case ItemKind::FuelCellGenerator:
        return true;
    default:
        return false;
    }
}

bool machine_crafts(ItemKind kind, CraftCategory category) {
    switch (kind) {
    case ItemKind::AsteroidCrusher: return category == CraftCategory::Crushing;
    case ItemKind::SolarFurnace:    return category == CraftCategory::Smelting;
    case ItemKind::ArcFurnace:
        return category == CraftCategory::Smelting || category == CraftCategory::ArcSmelting;
    case ItemKind::IceMelter:       return category == CraftCategory::Melting;
    case ItemKind::Electrolyzer:    return category == CraftCategory::Electrolysis;
    case ItemKind::ChemicalPlant:   return category == CraftCategory::Chemistry;
    case ItemKind::FabricatorMk1:
    case ItemKind::FabricatorMk2:
    case ItemKind::FabricatorMk3:   return category == CraftCategory::Assembly;
    default:                        return false;
    }
}

int machine_crafting_speed(ItemKind kind) {
    switch (kind) {
    case ItemKind::ArcFurnace:
    case ItemKind::FabricatorMk2: return 2;
    case ItemKind::FabricatorMk3: return 4;
    default:                      return 1;
    }
}

Building::Building(std::optional<ItemKind> k, Direction facing, GridSize s)
    : kind(k), dir(facing), size(s) {}

Bridge::Bridge() : Building(std::nullopt, Direction::North, GridSize{1, 1}) {}

bool Bridge::can_hand_craft(const Recipe& recipe) const {
    return recipe.hand_craftable && inventory.has(recipe.inputs);
}

bool Bridge::queue_craft(const Recipe& recipe) {
    if (!recipe.hand_craftable || !inventory.take(recipe.inputs))
        return false;
    queue.push_back({&recipe, 0});
    return true;
}

bool Bridge::cancel_craft(std::size_t index) {
    if (index >= queue.size())
        return false;
    inventory.add(queue[index].recipe->inputs);
    queue.erase(queue.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void Bridge::tick() {
    if (queue.empty())
        return;
    CraftJob& job = queue.front();
    if (++job.elapsed < job.recipe->ticks)
        return;
    inventory.add(job.recipe->outputs);
    queue.pop_front();
}

bool Machine::crafts(CraftCategory category) const {
    return machine_crafts(*item_kind(), category);
}

int Machine::crafting_speed() const {
    return machine_crafting_speed(*item_kind());
}

std::unique_ptr<Building> make_building(ItemKind kind, Direction facing) {
    switch (kind) {
    case ItemKind::AsteroidCollector:
    case ItemKind::TractorArray:
    case ItemKind::HydrogenCollector:
        return std::make_unique<Collector>(kind);
    case ItemKind::TransportBelt:
    case ItemKind::ExpressBelt:
    case ItemKind::BeltTunnel:
    case ItemKind::Splitter:
    case ItemKind::FilterSorter:
        return std::make_unique<Conveyor>(kind, facing);
    case ItemKind::CargoHold:
        return std::make_unique<CargoHold>();
    case ItemKind::Pipe:
    case ItemKind::Pump:
    case ItemKind::FluidTank:
        return std::make_unique<FluidPart>(kind, facing);
    case ItemKind::AsteroidCrusher:
    case ItemKind::SolarFurnace:
    case ItemKind::ArcFurnace:
    case ItemKind::IceMelter:
    case ItemKind::Electrolyzer:
    case ItemKind::ChemicalPlant:
    case ItemKind::FabricatorMk1:
    case ItemKind::FabricatorMk2:
    case ItemKind::FabricatorMk3:
        return std::make_unique<Machine>(kind);
    case ItemKind::SolarPanel:
    case ItemKind::SolarArray:
    case ItemKind::FuelCellGenerator:
    case ItemKind::BatteryBank:
        return std::make_unique<PowerPlant>(kind);
    case ItemKind::SurveyScanner:
    case ItemKind::IonThruster:
        return std::make_unique<ShipSystem>(kind);
    default:
        break;
    }
    assert(false && "make_building: not a building kind");
    return nullptr;
}
