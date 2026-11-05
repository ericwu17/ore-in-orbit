// building.cpp — the inventory, building data (footprints, rotation,
// machine categories, health), what each kind of building does, the bridge's
// hand-crafting and the building factory (see building.h).
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
    case ItemKind::CargoHold:
    case ItemKind::AsteroidCollector:
    case ItemKind::SolarFurnace:
    case ItemKind::SurveyScanner:
    case ItemKind::ShieldProjector:
    case ItemKind::BurstLaser:
    case ItemKind::HeavyLaser:
    case ItemKind::MissileLauncher:
    case ItemKind::IonBlaster:
        size = {2, 2};
        break;
    case ItemKind::TractorArray:
    case ItemKind::Refinery:
    case ItemKind::FabricatorMk1:
    case ItemKind::FabricatorMk2:
    case ItemKind::FabricatorMk3:
    case ItemKind::FlakCannon:
    case ItemKind::BeamEmitter:
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
    case ItemKind::Unloader:
        return true;
    default:
        return false;
    }
}

bool machine_crafts(ItemKind kind, CraftCategory category) {
    switch (kind) {
    case ItemKind::SolarFurnace:    return category == CraftCategory::Smelting;
    case ItemKind::Refinery:
        return category == CraftCategory::Smelting || category == CraftCategory::Refining;
    case ItemKind::FabricatorMk1:
    case ItemKind::FabricatorMk2:
    case ItemKind::FabricatorMk3:   return category == CraftCategory::Assembly;
    default:                        return false;
    }
}

int machine_crafting_speed(ItemKind kind) {
    switch (kind) {
    case ItemKind::Refinery:
    case ItemKind::FabricatorMk2: return 2;
    case ItemKind::FabricatorMk3: return 4;
    default:                      return 1;
    }
}

int max_hp_of(std::optional<ItemKind> kind) {
    if (!kind)
        return 2000;  // the bridge
    switch (*kind) {
    case ItemKind::TransportBelt:
    case ItemKind::ExpressBelt:
    case ItemKind::BeltTunnel:
    case ItemKind::FilterSorter:
    case ItemKind::ReverseSorter:
    case ItemKind::Junction:
    case ItemKind::Router:
    case ItemKind::Unloader:          return 50;
    case ItemKind::AsteroidCollector: return 150;
    case ItemKind::CargoHold:
    case ItemKind::SolarFurnace:
    case ItemKind::SurveyScanner:     return 200;
    case ItemKind::FabricatorMk1:     return 250;
    case ItemKind::IonThruster:       return 300;
    case ItemKind::TractorArray:
    case ItemKind::Refinery:
    case ItemKind::FabricatorMk2:
    case ItemKind::ShieldProjector:   return 400;
    case ItemKind::BurstLaser:
    case ItemKind::HeavyLaser:
    case ItemKind::MissileLauncher:
    case ItemKind::IonBlaster:        return 500;
    case ItemKind::FabricatorMk3:     return 600;
    case ItemKind::FlakCannon:
    case ItemKind::BeamEmitter:       return 800;
    default:                          return 100;
    }
}

Building::Building(std::optional<ItemKind> k, Direction facing, GridSize s)
    : placed_by(k), dir(facing), size(s), hp(max_hp_of(k)), max(hp) {}

void Building::set_max_hp(int full) {
    max = std::max(full, 1);
    hp = std::min(hp, max);
}

bool Building::take_damage(int amount) {
    hp = std::max(0, hp - std::max(amount, 0));
    return hp == 0;
}

void Building::repair(int amount) {
    hp = std::min(max_hp(), hp + std::max(amount, 0));
}

Bridge::Bridge() : Building(std::nullopt, Direction::North, GridSize{kBridgeSize, kBridgeSize}) {}

void Bridge::receive(ItemKind kind) {
    inventory.add(kind);
    progress->on_delivered(kind);
}

void Bridge::unload(ItemKind kind) {
    const bool taken = inventory.take(kind);
    assert(taken);
    (void)taken;
    progress->on_unloaded(kind);
}

bool Bridge::can_hand_craft(const Recipe& recipe) const {
    return recipe.hand_craftable && progress->unlocked(recipe) && inventory.has(recipe.inputs);
}

bool Bridge::queue_craft(const Recipe& recipe) {
    if (!can_hand_craft(recipe))
        return false;
    const bool taken = inventory.take(recipe.inputs);
    assert(taken);
    (void)taken;
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

std::optional<int> Bridge::request_for(ItemKind kind) const {
    const auto it = std::find_if(requests.begin(), requests.end(), [&](const ItemStack& r) { return r.kind == kind; });
    return it == requests.end() ? std::nullopt : std::optional<int>(it->count);
}

void Bridge::set_request(ItemKind kind, int count) {
    assert(count >= 0 && count <= kMaxRequest);
    const auto it = std::find_if(requests.begin(), requests.end(), [&](const ItemStack& r) { return r.kind == kind; });
    if (it == requests.end())
        requests.push_back({kind, count});
    else
        it->count = count;
}

void Bridge::remove_request(ItemKind kind) {
    std::erase_if(requests, [&](const ItemStack& r) { return r.kind == kind; });
}

bool Bridge::request_unmet(ItemKind kind) const {
    const std::optional<int> wanted = request_for(kind);
    return wanted && inventory.count(kind) < *wanted;
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
    using enum ItemKind;
    static const CollectorSpec asteroid{
        .interval = 2 * kTicksPerSecond, .capacity = 10, .targets = {IronOre, CopperOre, Silica, Carbon}};
    // The only source of iridium.
    static const CollectorSpec tractor{
        .interval = kTicksPerSecond / 2,
        .capacity = 20,
        .targets = {IronOre, CopperOre, Silica, Carbon, IridiumOre}};
    switch (kind) {
    case AsteroidCollector: return asteroid;
    case TractorArray:      return tractor;
    default:                break;
    }
    assert(false && "collector_spec: not a collector");
    return asteroid;
}

Collector::Collector(ItemKind k)
    : Building(k, Direction::North, footprint_of(k)), spec(&collector_spec(k)), target(spec->targets.front()) {}

bool Collector::can_target(ItemKind kind) const {
    return std::find(spec->targets.begin(), spec->targets.end(), kind) != spec->targets.end();
}

void Collector::set_target(ItemKind kind) {
    assert(can_target(kind));
    target = kind;
}

void Collector::tick() {
    if (full() || standby)
        return;
    progress += static_cast<Tick>(yield);
    if (progress < spec->interval * 100)
        return;
    progress -= spec->interval * 100;
    caught.add(target);
}

double Collector::rate_per_second() const {
    return static_cast<double>(kTicksPerSecond) * yield / 100.0 / static_cast<double>(spec->interval);
}

float Collector::catch_progress() const {
    return static_cast<float>(progress) / static_cast<float>(spec->interval * 100);
}

const WeaponSpec& weapon_spec(ItemKind kind) {
    constexpr Tick s = kTicksPerSecond;
    // Two quick shots: the weapon a new game starts with, and the weakest.
    static const WeaponSpec burst_laser{.family = WeaponFamily::Laser, .ammo = ItemKind::FocusCrystal,
                                        .shots = 2, .damage = 60, .charge = 10 * s, .flight = s,
                                        .blurb = "Two laser shots. Each is stopped by a shield layer, and pops it."};
    static const WeaponSpec heavy_laser{.family = WeaponFamily::Laser, .ammo = ItemKind::FocusCrystal,
                                        .shots = 2, .damage = 170, .charge = 11 * s, .flight = s,
                                        .blurb = "Two heavy laser shots. Each is stopped by a shield layer, and pops it."};
    static const WeaponSpec missile_launcher{.family = WeaponFamily::Missile, .ammo = ItemKind::Missile,
                                             .shots = 1, .damage = 350, .charge = 11 * s, .flight = 2 * s,
                                             .blurb = "One missile, slow but heavy. It flies straight through shields."};
    static const WeaponSpec ion_blaster{.family = WeaponFamily::Ion, .ammo = ItemKind::IonCell,
                                        .shots = 1, .damage = 30, .charge = 7 * s, .flight = s, .stun = 10 * s,
                                        .blurb = "An ion bolt: knocks down a shield layer and stuns its projector, "
                                                 "or stuns the weapon or projector it hits."};
    static const WeaponSpec flak_cannon{.family = WeaponFamily::Flak, .ammo = ItemKind::FlakShell,
                                        .shots = 7, .damage = 50, .charge = 14 * s, .flight = s, .spread = 4,
                                        .blurb = "A spray of pellets round the target: each is stopped by a shield "
                                                 "layer, so it strips shields fast."};
    static const WeaponSpec beam_emitter{.family = WeaponFamily::Beam, .ammo = ItemKind::IridiumLens,
                                         .shots = 1, .damage = 180, .charge = 15 * s, .flight = s / 2, .spread = 10,
                                         .blurb = "Sweeps a beam across the ship, hitting every building on its line. "
                                                  "Never misses; each shield layer up weakens every hit."};
    switch (kind) {
    case ItemKind::BurstLaser:      return burst_laser;
    case ItemKind::HeavyLaser:      return heavy_laser;
    case ItemKind::MissileLauncher: return missile_launcher;
    case ItemKind::IonBlaster:      return ion_blaster;
    case ItemKind::FlakCannon:      return flak_cannon;
    case ItemKind::BeamEmitter:     return beam_emitter;
    default:                        break;
    }
    assert(false && "weapon_spec: not a weapon");
    return burst_laser;
}

Weapon::Weapon(ItemKind k) : Building(k, Direction::North, footprint_of(k)), spec(&weapon_spec(k)) {}

float Weapon::charge_fraction() const {
    const int full = static_cast<int>(spec->charge) * kChargeScale;
    return full > 0 ? std::min(1.0f, static_cast<float>(charge) / static_cast<float>(full)) : 1.0f;
}

void Weapon::charge_up(bool free, int pace) {
    if (stun > 0 || charged() || (!free && !powered()))
        return;
    charge = std::min(charge + pace, static_cast<int>(spec->charge) * kChargeScale);
}

void Weapon::fire(bool free) {
    assert(charged());
    if (!free) {
        assert(powered());
        --fuel;
        --ammo;
    }
    charge = 0;
}

void Weapon::tick() {
    if (stun > 0)
        --stun;
}

std::vector<ItemStack> Weapon::contents() const {
    std::vector<ItemStack> held;
    if (fuel > 0)
        held.push_back({ItemKind::FuelCell, fuel});
    if (ammo > 0)
        held.push_back({spec->ammo, ammo});
    return held;
}

bool Weapon::accepts(ItemKind kind) const {
    if (kind == ItemKind::FuelCell)
        return fuel < kWeaponFuelCapacity;
    return kind == spec->ammo && ammo < kWeaponAmmoCapacity;
}

void Weapon::receive(ItemKind kind) {
    if (kind == ItemKind::FuelCell)
        ++fuel;
    else
        ++ammo;
}

void ShieldProjector::tick() {
    if (stun > 0)
        --stun;
}

std::vector<ItemStack> Thruster::contents() const {
    if (fuel == 0)
        return {};
    return {{ItemKind::FuelCell, fuel}};
}

int belt_speed(ItemKind kind) {
    switch (kind) {
    case ItemKind::TransportBelt: return 2;  // 2 cells per second
    case ItemKind::ExpressBelt:
    case ItemKind::BeltTunnel:    return 4;  // one tier: it never holds up a line
    default:                      return 0;
    }
}

void Conveyor::tick() {
    int limit = length();
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
    for (const BeltItem& item : items)
        if (std::abs(item.pos - pos) < kBeltSpacing)
            return false;
    return true;
}

void Conveyor::insert(ItemKind kind, int pos, BeltSide from) {
    assert(has_room_at(pos));
    auto it = items.begin();
    while (it != items.end() && it->pos > pos)
        ++it;
    items.insert(it, BeltItem{kind, pos, from});
}

std::optional<ItemKind> Conveyor::item_at_front() const {
    if (items.empty() || items.front().pos < length())
        return std::nullopt;
    return items.front().kind;
}

void Conveyor::pop_front() {
    assert(!items.empty());
    items.erase(items.begin());
}

void Tunnel::unlink() {
    exit_cell.reset();
    entrance_cell.reset();
    distance = 0;
}

void Tunnel::link_exit(GridPos exit, int cells) {
    exit_cell = exit;
    distance = cells;
}

void Tunnel::link_entrance(GridPos entrance, int cells) {
    entrance_cell = entrance;
    distance = cells;
}

void Junction::tick() {
    for (std::deque<JunctionItem>& lane : lanes)
        for (JunctionItem& item : lane)
            item.elapsed = std::min(item.elapsed + 1, kJunctionTicks);
}

std::vector<ItemStack> Junction::contents() const {
    Inventory held;
    for (const std::deque<JunctionItem>& lane : lanes)
        for (const JunctionItem& item : lane)
            held.add(item.kind);
    return held.stacks();
}

void Junction::insert(ItemKind kind, Direction travel) {
    assert(has_room(travel));
    lanes[static_cast<std::size_t>(travel)].push_back({kind, 0});
}

std::optional<ItemKind> Junction::item_ready(Direction travel) const {
    const std::deque<JunctionItem>& l = lane(travel);
    if (l.empty() || l.front().elapsed < kJunctionTicks)
        return std::nullopt;
    return l.front().kind;
}

void Junction::pop(Direction travel) {
    std::deque<JunctionItem>& l = lanes[static_cast<std::size_t>(travel)];
    assert(!l.empty());
    l.pop_front();
}

void Router::tick() {
    if (held)
        elapsed = std::min(elapsed + 1, kRouterTicks);
}

std::vector<ItemStack> Router::contents() const {
    if (!held)
        return {};
    return {{*held, 1}};
}

void Router::insert(ItemKind kind, Direction from) {
    assert(empty());
    held = kind;
    came_from = from;
    elapsed = 0;
}

std::optional<ItemKind> Router::item_ready() const {
    return elapsed >= kRouterTicks ? held : std::nullopt;
}

void Router::pop() {
    assert(held);
    held.reset();
    elapsed = 0;
}

void Unloader::tick() {
    if (cooldown > 0)
        --cooldown;
}

void Unloader::unloaded(ItemKind kind) {
    cooldown = kUnloaderTicks;
    next_kind = (static_cast<std::size_t>(kind) + 1) % static_cast<std::size_t>(kItemKindCount);
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
    work.reset();
    recipe = r;
    return refund;
}

bool Machine::can_start() const {
    if (standby || !recipe || !input.has(recipe->inputs))
        return false;
    for (const ItemStack& s : recipe->outputs)
        if (output.count(s.kind) + s.count > s.count * kOutputCrafts)
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
        work = 0;
    }
    *work += static_cast<Tick>(crafting_speed());
    if (*work < recipe->ticks)
        return;
    output.add(recipe->outputs);
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

std::optional<ItemKind> product_of(const Building& b) {
    if (const auto* collector = dynamic_cast<const Collector*>(&b))
        return collector->get_target();
    if (const auto* machine = dynamic_cast<const Machine*>(&b))
        if (const Recipe* recipe = machine->get_recipe())
            return recipe->outputs.front().kind;
    return std::nullopt;
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
        return std::make_unique<Collector>(kind);
    case ItemKind::TransportBelt:
    case ItemKind::ExpressBelt:
        return std::make_unique<Conveyor>(kind, facing);
    case ItemKind::BeltTunnel:
        return std::make_unique<Tunnel>(facing);
    case ItemKind::FilterSorter:
    case ItemKind::ReverseSorter:
        return std::make_unique<Sorter>(kind);
    case ItemKind::Junction:
        return std::make_unique<Junction>();
    case ItemKind::Router:
        return std::make_unique<Router>();
    case ItemKind::Unloader:
        return std::make_unique<Unloader>(facing);
    case ItemKind::CargoHold:
        return std::make_unique<CargoHold>();
    case ItemKind::SolarFurnace:
    case ItemKind::Refinery:
    case ItemKind::FabricatorMk1:
    case ItemKind::FabricatorMk2:
    case ItemKind::FabricatorMk3:
        return std::make_unique<Machine>(kind);
    case ItemKind::IonThruster:
        return std::make_unique<Thruster>();
    case ItemKind::BurstLaser:
    case ItemKind::HeavyLaser:
    case ItemKind::MissileLauncher:
    case ItemKind::IonBlaster:
    case ItemKind::FlakCannon:
    case ItemKind::BeamEmitter:
        return std::make_unique<Weapon>(kind);
    case ItemKind::ShieldProjector:
        return std::make_unique<ShieldProjector>();
    case ItemKind::SurveyScanner:
        return std::make_unique<ShipSystem>(kind);
    default:
        break;
    }
    assert(false && "make_building: not a building kind");
    return nullptr;
}
