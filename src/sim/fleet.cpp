// fleet.cpp — launching ships, cargo pods and ticking the fleet (see
// fleet.h).
#include "sim/fleet.h"

#include <algorithm>
#include <cassert>
#include <utility>

const char* describe(FleetResult result) {
    switch (result) {
    case FleetResult::Ok:             return "Ok";
    case FleetResult::NoSuchShip:     return "No such ship";
    case FleetResult::SameShip:       return "A ship can't send cargo to itself";
    case FleetResult::OtherSector:    return "Ships can only trade within a sector";
    case FleetResult::NoStarterPack:  return "Needs a ship starter pack in the bridge inventory";
    case FleetResult::NothingToSend:  return "Nothing to send";
    case FleetResult::NotEnoughItems: return "Not that many in the bridge inventory";
    case FleetResult::NotEnoughPods:  return "Not enough cargo pods in the bridge inventory";
    }
    return "?";
}

int pods_needed(int items) {
    return (std::max(items, 0) + kPodCapacity - 1) / kPodCapacity;
}

Fleet Fleet::new_game() {
    Fleet fleet;
    fleet.add("Flagship", Ship::new_game(), 0);
    return fleet;
}

FleetShip& Fleet::add(std::string name, Ship ship, int sector) {
    ship.get_bridge().share_progress(progress);
    auto added = std::make_unique<FleetShip>(FleetShip{next_id++, std::move(name), std::move(ship), sector});
    ships.push_back(std::move(added));
    return *ships.back();
}

FleetShip* Fleet::find(int id) {
    for (const std::unique_ptr<FleetShip>& s : ships)
        if (s->id == id)
            return s.get();
    return nullptr;
}

const FleetShip* Fleet::find(int id) const {
    return const_cast<Fleet*>(this)->find(id);
}

FleetResult Fleet::can_launch(int from) const {
    const FleetShip* source = find(from);
    if (!source)
        return FleetResult::NoSuchShip;
    if (source->ship.inventory().count(ItemKind::ShipStarterPack) <= 0)
        return FleetResult::NoStarterPack;
    return FleetResult::Ok;
}

FleetResult Fleet::launch(int from, int* launched) {
    const FleetResult result = can_launch(from);
    if (result != FleetResult::Ok)
        return result;
    FleetShip& source = *find(from);
    const bool taken = source.ship.inventory().take(ItemKind::ShipStarterPack);
    assert(taken);
    (void)taken;
    const int sector = source.sector;  // `source` may move when the list grows
    FleetShip& added = add("Ship " + std::to_string(next_id), Ship::new_launched(), sector);
    if (launched)
        *launched = added.id;
    return FleetResult::Ok;
}

namespace {

// `items` with the pods to carry them added, as the sender's inventory has
// to hold them.
std::vector<ItemStack> with_pods(const std::vector<ItemStack>& items) {
    int n = 0;
    for (const ItemStack& s : items)
        n += s.count;
    std::vector<ItemStack> all = items;
    all.push_back({ItemKind::CargoPod, pods_needed(n)});
    return all;
}

}  // namespace

FleetResult Fleet::can_send(int from, int to, const std::vector<ItemStack>& items) const {
    const FleetShip* source = find(from);
    const FleetShip* target = find(to);
    if (!source || !target)
        return FleetResult::NoSuchShip;
    if (from == to)
        return FleetResult::SameShip;
    if (source->sector != target->sector)
        return FleetResult::OtherSector;
    int n = 0;
    for (const ItemStack& s : items) {
        if (s.count < 0)
            return FleetResult::NothingToSend;
        n += s.count;
    }
    if (n == 0)
        return FleetResult::NothingToSend;
    const Inventory& inv = source->ship.inventory();
    if (!inv.has(items))
        return FleetResult::NotEnoughItems;
    if (!inv.has(with_pods(items)))
        return FleetResult::NotEnoughPods;
    return FleetResult::Ok;
}

FleetResult Fleet::send(int from, int to, const std::vector<ItemStack>& items, Tick now) {
    const FleetResult result = can_send(from, to, items);
    if (result != FleetResult::Ok)
        return result;
    const bool taken = find(from)->ship.inventory().take(with_pods(items));
    assert(taken);
    (void)taken;
    std::vector<ItemStack> cargo;
    for (const ItemStack& s : items)
        if (s.count > 0)
            cargo.push_back(s);
    transfers.push_back({from, to, std::move(cargo), now + kTransferTicks});
    return FleetResult::Ok;
}

bool Fleet::rename(int id, std::string name) {
    FleetShip* s = find(id);
    if (!s)
        return false;
    s->name = std::move(name);
    return true;
}

void Fleet::tick(Tick now) {
    for (const std::unique_ptr<FleetShip>& s : ships)
        s->ship.tick();
    std::erase_if(transfers, [&](const Transfer& t) {
        if (t.arrives > now)
            return false;
        if (FleetShip* target = find(t.to))
            target->ship.inventory().add(t.items);
        return true;
    });
}
