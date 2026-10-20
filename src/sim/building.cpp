// building.cpp — the inventory, building data (footprints, rotation,
// machine categories), the bridge's hand-crafting and the building factory
// (see building.h).
#include "sim/building.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
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

int Inventory::total() const {
    int sum = 0;
    for (const int n : counts)
        sum += n;
    return sum;
}

std::vector<ItemStack> Inventory::stacks() const {
    std::vector<ItemStack> out;
    for (std::size_t i = 0; i < counts.size(); ++i)
        if (counts[i] > 0)
            out.push_back({static_cast<ItemKind>(i), counts[i]});
    return out;
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

Bridge::Bridge() : Building(std::nullopt, Direction::North, GridSize{kBridgeSize, kBridgeSize}) {}

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

const CollectorSpec& collector_spec(ItemKind kind) {
    // Chunk weights: metallic, silicate, carbonaceous, ice, exotic.
    static const CollectorSpec asteroid{
        .interval = 2 * kTicksPerSecond, .capacity = 10, .chunk_weights = {7, 5, 4, 3, 1}};
    static const CollectorSpec tractor{
        .interval = kTicksPerSecond / 2, .capacity = 20, .chunk_weights = {6, 4, 4, 3, 3}};
    static const CollectorSpec hydrogen{
        .interval = kTicksPerSecond / 2, .capacity = 100, .fluid = FluidKind::Hydrogen, .fluid_amount = 5};
    switch (kind) {
    case ItemKind::AsteroidCollector: return asteroid;
    case ItemKind::TractorArray:      return tractor;
    case ItemKind::HydrogenCollector: return hydrogen;
    default:                          break;
    }
    assert(false && "collector_spec: not a collector");
    return asteroid;
}

Collector::Collector(ItemKind k)
    : Building(k, Direction::North, footprint_of(k)), spec(&collector_spec(k)) {}

void Collector::tick() {
    if (full())
        return;
    if (++progress < spec->interval)
        return;
    progress = 0;
    if (spec->fluid) {
        fluid = std::min(fluid + spec->fluid_amount, spec->capacity);
        return;
    }
    // Smooth weighted round robin: every kind gains its weight, the one
    // furthest ahead is caught and pays back the total.
    int total = 0;
    std::size_t pick = 0;
    for (std::size_t i = 0; i < chunk_turn.size(); ++i) {
        chunk_turn[i] += spec->chunk_weights[i];
        total += spec->chunk_weights[i];
        if (chunk_turn[i] > chunk_turn[pick])
            pick = i;
    }
    chunk_turn[pick] -= total;
    caught.add(static_cast<ItemKind>(static_cast<int>(ItemKind::MetallicChunk) + static_cast<int>(pick)));
}

double Collector::rate_per_second() const {
    const double per_catch = spec->fluid ? spec->fluid_amount : 1.0;
    return per_catch * static_cast<double>(kTicksPerSecond) / static_cast<double>(spec->interval);
}

float Collector::catch_progress() const {
    return static_cast<float>(progress) / static_cast<float>(spec->interval);
}

int belt_speed(ItemKind kind) {
    switch (kind) {
    case ItemKind::TransportBelt: return 2;  // 2 cells per second
    case ItemKind::ExpressBelt:   return 4;
    default:                      return 0;
    }
}

void Conveyor::tick() {
    int limit = kBeltLength;
    for (BeltItem& item : items) {
        item.pos = std::max(item.pos, std::min(item.pos + speed(), limit));
        limit = item.pos - kBeltSpacing;
    }
}

std::vector<ItemStack> Conveyor::contents() const {
    Inventory held;
    for (const BeltItem& item : items)
        held.add(item.kind);
    return held.stacks();
}

bool Conveyor::has_room_at(int pos) const {
    if (!moves_items())
        return false;
    for (const BeltItem& item : items)
        if (std::abs(item.pos - pos) < kBeltSpacing)
            return false;
    return true;
}

void Conveyor::insert(ItemKind kind, int pos) {
    assert(has_room_at(pos));
    auto it = items.begin();
    while (it != items.end() && it->pos > pos)
        ++it;
    items.insert(it, BeltItem{kind, pos});
}

std::optional<ItemKind> Conveyor::item_at_front() const {
    if (items.empty() || items.front().pos < kBeltLength)
        return std::nullopt;
    return items.front().kind;
}

void Conveyor::pop_front() {
    assert(!items.empty());
    items.erase(items.begin());
}

bool Machine::crafts(CraftCategory category) const {
    return machine_crafts(*item_kind(), category);
}

int Machine::crafting_speed() const {
    return machine_crafting_speed(*item_kind());
}

std::vector<ItemStack> Machine::set_recipe(const Recipe* r) {
    assert(!r || can_run(*r));
    std::vector<ItemStack> refund = input.stacks();
    if (work)
        refund.insert(refund.end(), recipe->inputs.begin(), recipe->inputs.end());
    input = Inventory{};
    fluid_in = {};
    work.reset();
    recipe = r;
    return refund;
}

bool Machine::can_start() const {
    if (!recipe || !input.has(recipe->inputs))
        return false;
    for (const FluidStack& f : recipe->fluid_inputs)
        if (fluid_input(f.kind) < f.amount)
            return false;
    for (const ItemStack& s : recipe->outputs)
        if (output.count(s.kind) + s.count > s.count * kOutputCrafts)
            return false;
    for (const FluidStack& f : recipe->fluid_outputs)
        if (fluid_output(f.kind) + f.amount > f.amount * kOutputCrafts)
            return false;
    return true;
}

void Machine::tick() {
    if (!work) {
        if (!can_start())
            return;
        const bool taken = input.take(recipe->inputs);
        assert(taken);
        (void)taken;
        for (const FluidStack& f : recipe->fluid_inputs)
            fluid_in[static_cast<std::size_t>(f.kind)] -= f.amount;
        work = 0;
    }
    *work += static_cast<Tick>(crafting_speed());
    if (*work < recipe->ticks)
        return;
    output.add(recipe->outputs);
    for (const FluidStack& f : recipe->fluid_outputs)
        fluid_out[static_cast<std::size_t>(f.kind)] += f.amount;
    work.reset();
}

std::vector<ItemStack> Machine::contents() const {
    std::vector<ItemStack> held = input.stacks();
    const std::vector<ItemStack> made = output.stacks();
    held.insert(held.end(), made.begin(), made.end());
    if (work)
        held.insert(held.end(), recipe->inputs.begin(), recipe->inputs.end());
    return held;
}

bool Machine::accepts(ItemKind kind) const {
    if (!recipe)
        return false;
    for (const ItemStack& s : recipe->inputs)
        if (s.kind == kind)
            return input.count(kind) < s.count * kInputCrafts;
    return false;
}

float Machine::craft_progress() const {
    return work ? static_cast<float>(*work) / static_cast<float>(recipe->ticks) : 0.0f;
}

double Machine::crafts_per_minute() const {
    if (!recipe)
        return 0.0;
    return 60.0 * static_cast<double>(kTicksPerSecond) * crafting_speed() / static_cast<double>(recipe->ticks);
}

std::vector<const Recipe*> machine_recipes(ItemKind kind) {
    std::vector<const Recipe*> list;
    for (const Recipe& r : all_recipes())
        if (machine_crafts(kind, r.category))
            list.push_back(&r);
    return list;
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
        return std::make_unique<Conveyor>(kind, facing);
    case ItemKind::FilterSorter:
        return std::make_unique<Sorter>();
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
