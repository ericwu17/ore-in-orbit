// fleet.cpp — launching ships, cargo pods and ticking the fleet (see
// fleet.h).
#include "sim/fleet.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <utility>

const char* describe(FleetResult result) {
    switch (result) {
    case FleetResult::Ok:             return "Ok";
    case FleetResult::NoSuchShip:     return "No such ship";
    case FleetResult::SameShip:       return "A ship can't send cargo to itself";
    case FleetResult::OtherSector:    return "Ships can only trade within a sector";
    case FleetResult::LayoutLocked:   return "Not unlocked yet";
    case FleetResult::NoStarterPack:  return "Needs a ship starter pack in the bridge inventory";
    case FleetResult::NotEnoughForLayout: return "Not everything it costs is in the bridge inventory";
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
    // The flagship's burst laser (see Ship::new_game) is the one weapon
    // the fleet starts with; the rest it finds.
    fleet.progress->add_found(ItemKind::BurstLaser);
    return fleet;
}

FleetShip& Fleet::add(std::string name, Ship ship, int sector) {
    ship.get_bridge().share_progress(progress);
    auto added = std::make_unique<FleetShip>(FleetShip{next_id++, std::move(name), std::move(ship), sector, {}});
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

namespace {

// What building a ship in `layout` takes: its cost and a starter pack.
std::vector<ItemStack> launch_cost(const ShipLayout& layout) {
    std::vector<ItemStack> all = layout.cost;
    all.push_back({ItemKind::ShipStarterPack, 1});
    return all;
}

}  // namespace

FleetResult Fleet::can_launch(int from, const ShipLayout& layout) const {
    const FleetShip* source = find(from);
    if (!source)
        return FleetResult::NoSuchShip;
    if (!layout_unlocked(layout, *progress))
        return FleetResult::LayoutLocked;
    const Inventory& inv = source->ship.inventory();
    if (inv.count(ItemKind::ShipStarterPack) <= 0)
        return FleetResult::NoStarterPack;
    if (!inv.has(launch_cost(layout)))
        return FleetResult::NotEnoughForLayout;
    return FleetResult::Ok;
}

FleetResult Fleet::launch(int from, const ShipLayout& layout, int* launched) {
    const FleetResult result = can_launch(from, layout);
    if (result != FleetResult::Ok)
        return result;
    FleetShip& source = *find(from);
    const bool taken = source.ship.inventory().take(launch_cost(layout));
    assert(taken);
    (void)taken;
    Ship ship = Ship::from_layout(layout);
    FleetShip& added = add("Ship " + std::to_string(next_id), std::move(ship), source.sector);
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

void Fleet::remove(int id) {
    std::erase_if(ships, [&](const std::unique_ptr<FleetShip>& s) { return s->id == id; });
}

int Fleet::in_flight_to(int to, ItemKind kind) const {
    int n = 0;
    for (const Transfer& t : transfers)
        if (t.to == to)
            for (const ItemStack& s : t.items)
                if (s.kind == kind)
                    n += s.count;
    return n;
}

bool Fleet::set_provides(int id, bool on) {
    FleetShip* s = find(id);
    if (!s)
        return false;
    s->provides = on;
    return true;
}

bool Fleet::short_of_pods(int id) const {
    return std::find(out_of_pods.begin(), out_of_pods.end(), id) != out_of_pods.end();
}

namespace {

// What a provider can give of `kind`: what its bridge holds beyond its own
// request, less what it has already promised this dispatch.
int surplus(const FleetShip& provider, ItemKind kind, const Inventory& promised) {
    const Bridge& bridge = provider.ship.get_bridge();
    const int spare = bridge.get_inventory().count(kind) - bridge.request_for(kind).value_or(0) - promised.count(kind);
    return std::max(spare, 0);
}

int total_of(const std::vector<ItemStack>& items) {
    int n = 0;
    for (const ItemStack& s : items)
        n += s.count;
    return n;
}

// The first `n` items of `items`.
std::vector<ItemStack> first_items(const std::vector<ItemStack>& items, int n) {
    std::vector<ItemStack> kept;
    for (const ItemStack& s : items) {
        if (n <= 0)
            break;
        kept.push_back({s.kind, std::min(s.count, n)});
        n -= kept.back().count;
    }
    return kept;
}

// Cargo one provider is to send one requester at a dispatch.
struct Shipment {
    int from = 0;
    int to = 0;
    std::vector<ItemStack> items;
    // Whether every item in it leaves its requester's request met (counting
    // what other providers send): a part-filled pod of it needn't wait.
    bool completes = true;
};

// How many cargo pods provider `from` can send requester `to` as cargo,
// when `spare` is its surplus of them: the pods that carry its shipments
// come from the same stock, so it keeps back enough for those (the ones
// already planned, and this one with the pods added).
int pods_to_spare(int spare, const std::vector<Shipment>& shipments, int from, int to) {
    int planned = 0;  // items already going to `to`
    for (const Shipment& s : shipments) {
        if (s.from != from)
            continue;
        if (s.to == to)
            planned = total_of(s.items);
        else
            spare -= pods_needed(total_of(s.items));
    }
    if (spare <= 0)
        return 0;
    // The most n with n pods sent and the pods to carry everything kept.
    const auto fits = [&](int n) { return n + pods_needed(planned + n) <= spare; };
    int n = std::max(spare - pods_needed(planned + spare), 0);
    while (fits(n + 1))
        ++n;
    return fits(n) ? n : 0;
}

// How many more items provider `from` has the pods to carry to requester
// `to`, when `carriers` of its pods aren't promised as cargo: what's left
// once its other shipments have their pods, less what's already going to
// `to`.
int room_to_carry(int carriers, const std::vector<Shipment>& shipments, int from, int to) {
    int planned = 0;
    for (const Shipment& s : shipments) {
        if (s.from != from)
            continue;
        if (s.to == to)
            planned = total_of(s.items);
        else
            carriers -= pods_needed(total_of(s.items));
    }
    // Wide, for a provider with a great many pods.
    const long long room = static_cast<long long>(std::max(carriers, 0)) * kPodCapacity - planned;
    return static_cast<int>(std::clamp<long long>(room, 0, std::numeric_limits<int>::max()));
}

}  // namespace

void Fleet::dispatch(Tick now) {
    out_of_pods.clear();
    std::vector<Shipment> shipments;
    // Provider-to-requester pairs with cargo held back for want of pods.
    std::vector<std::pair<int, int>> held;
    // What each provider has promised this dispatch, by ship index.
    std::vector<Inventory> promised(ships.size());
    for (std::size_t r = 0; r < ships.size(); ++r) {
        const FleetShip& requester = *ships[r];
        const Bridge& bridge = requester.ship.get_bridge();
        for (const ItemStack& request : bridge.get_requests()) {
            int shortfall = request.count - bridge.get_inventory().count(request.kind) -
                            in_flight_to(requester.id, request.kind);
            std::vector<std::size_t> givers;  // the shipments it went into
            std::vector<int> pod_bound;  // providers with more than their pods carry
            while (shortfall > 0) {
                // The provider in the same sector with the most to spare;
                // ties go to the oldest.
                std::size_t best = ships.size();
                int best_spare = 0;
                for (std::size_t p = 0; p < ships.size(); ++p) {
                    const FleetShip& provider = *ships[p];
                    if (p == r || !provider.provides || provider.sector != requester.sector)
                        continue;
                    const int had = surplus(provider, request.kind, promised[p]);
                    int spare = had;
                    // Only what its pods can carry, so a provider without
                    // pods leaves the request to one with them.
                    if (request.kind == ItemKind::CargoPod) {
                        spare = pods_to_spare(spare, shipments, provider.id, requester.id);
                    } else {
                        const int carriers = provider.ship.inventory().count(ItemKind::CargoPod) -
                                             promised[p].count(ItemKind::CargoPod);
                        spare = std::min(spare, room_to_carry(carriers, shipments, provider.id, requester.id));
                        if (spare < std::min(had, shortfall) &&
                            std::find(pod_bound.begin(), pod_bound.end(), provider.id) == pod_bound.end())
                            pod_bound.push_back(provider.id);
                    }
                    if (spare > best_spare) {
                        best = p;
                        best_spare = spare;
                    }
                }
                if (best == ships.size())
                    break;
                const int n = std::min(shortfall, best_spare);
                promised[best].add(request.kind, n);
                shortfall -= n;
                const int from = ships[best]->id;
                auto it = std::find_if(shipments.begin(), shipments.end(),
                                       [&](const Shipment& s) { return s.from == from && s.to == requester.id; });
                if (it == shipments.end()) {
                    shipments.push_back({from, requester.id, {}, true});
                    it = shipments.end() - 1;
                }
                it->items.push_back({request.kind, n});
                givers.push_back(static_cast<std::size_t>(it - shipments.begin()));
            }
            if (shortfall > 0) {
                for (const std::size_t g : givers)
                    shipments[g].completes = false;
                // Left short for want of pods: those providers say so, and
                // what they hold back for want of them waits as a part-filled
                // pod would, to go as soon as they have one.
                for (const int id : pod_bound) {
                    if (!short_of_pods(id))
                        out_of_pods.push_back(id);
                    if (std::find(held.begin(), held.end(), std::pair{id, requester.id}) == held.end())
                        held.push_back({id, requester.id});
                }
            }
        }
    }

    std::vector<std::pair<std::pair<int, int>, Tick>> still_partial;
    for (const Shipment& s : shipments) {
        const std::pair<int, int> pair{s.from, s.to};
        const auto waiting = std::find_if(partial_since.begin(), partial_since.end(),
                                          [&](const auto& w) { return w.first == pair; });
        const bool waited = waiting != partial_since.end() && now - waiting->second >= kPartialPodWait;
        const int total = total_of(s.items);
        // Whole pods go at once; a part-filled one only if it finishes the
        // job or has waited long enough.
        const int n = (s.completes || waited) ? total : total / kPodCapacity * kPodCapacity;
        // Short of pods for all of it, whether or not it all goes now.
        if (can_send(s.from, s.to, s.items) != FleetResult::Ok && !short_of_pods(s.from))
            out_of_pods.push_back(s.from);
        // As much of it as the provider's pods carry: the more items, the
        // more pods it takes, so the most that fits is found by halving.
        int lo = 0;
        int hi = n;
        while (lo < hi) {
            const int mid = lo + (hi - lo + 1) / 2;
            if (can_send(s.from, s.to, first_items(s.items, mid)) == FleetResult::Ok)
                lo = mid;
            else
                hi = mid - 1;
        }
        if (lo > 0) {
            const FleetResult sent = send(s.from, s.to, first_items(s.items, lo), now);
            assert(sent == FleetResult::Ok);
            (void)sent;
            transfers.back().automatic = true;
        }
        // What didn't go waits on, from when it started waiting.
        if (lo < total)
            still_partial.push_back({pair, waiting != partial_since.end() ? waiting->second : now});
    }
    for (const std::pair<int, int>& pair : held) {
        const bool listed = std::any_of(still_partial.begin(), still_partial.end(),
                                        [&](const auto& w) { return w.first == pair; });
        const bool shipped = std::any_of(shipments.begin(), shipments.end(),
                                         [&](const Shipment& s) { return std::pair{s.from, s.to} == pair; });
        if (listed || shipped)
            continue;
        const auto waiting = std::find_if(partial_since.begin(), partial_since.end(),
                                          [&](const auto& w) { return w.first == pair; });
        still_partial.push_back({pair, waiting != partial_since.end() ? waiting->second : now});
    }
    partial_since = std::move(still_partial);
}

void Fleet::link(Tick now) {
    std::vector<std::pair<int, Provider*>> providers;  // with its sector
    std::vector<std::pair<int, Requester*>> requesters;
    for (const std::unique_ptr<FleetShip>& s : ships) {
        for (const GridPos p : s->ship.placement_order()) {
            Building* b = s->ship.building_anchored_at(p);
            if (auto* provider = dynamic_cast<Provider*>(b))
                providers.push_back({s->sector, provider});
            else if (auto* requester = dynamic_cast<Requester*>(b))
                requesters.push_back({s->sector, requester});
        }
    }
    if (providers.empty() || requesters.empty())
        return;
    // Longest waiting first; stable, so ties keep fleet order.
    std::stable_sort(requesters.begin(), requesters.end(), [](const auto& a, const auto& b) {
        return a.second->get_last_received() < b.second->get_last_received();
    });
    for (const auto& [sector, requester] : requesters) {
        if (!requester->wants())
            continue;
        const ItemKind kind = *requester->get_filter();
        Provider* best = nullptr;
        int most = 0;
        for (const auto& [provider_sector, provider] : providers) {
            const int held = provider->get_stored().count(kind);
            if (provider_sector == sector && provider->ready() && held > most) {
                best = provider;
                most = held;
            }
        }
        if (best) {
            best->sent(kind);
            requester->received(kind, now);
        }
    }
}

void Fleet::deliver(EventLog& events, const StarMap& map) {
    for (const std::unique_ptr<FleetShip>& s : ships) {
        std::vector<std::size_t> here = map.deliveries_at(s->sector);
        std::erase_if(here, [&](std::size_t d) { return !progress->open(d); });
        if (here.empty())
            continue;
        for (const GridPos p : s->ship.placement_order()) {
            auto* provider = dynamic_cast<Provider*>(s->ship.building_anchored_at(p));
            if (!provider || !provider->ready())
                continue;
            const auto hand_over = [&] {
                for (const std::size_t d : here)
                    for (const ItemStack& want : all_deliveries()[d].wants)
                        if (provider->get_stored().count(want.kind) > 0 && progress->wanted(d, want.kind) > 0) {
                            provider->sent(want.kind);
                            if (progress->deliver(d, want.kind))
                                events.push({.kind = EventKind::DeliveryComplete,
                                             .ship = s->id,
                                             .sector = s->sector,
                                             .delivery = static_cast<int>(d)});
                            return;
                        }
            };
            hand_over();
            std::erase_if(here, [&](std::size_t d) { return !progress->open(d); });
            if (here.empty())
                break;
        }
    }
}

void Fleet::limit_providers(const StarMap& map) {
    std::vector<std::vector<Provider*>> providers(static_cast<std::size_t>(map.size()));
    std::vector<std::vector<ItemKind>> pulled(static_cast<std::size_t>(map.size()));
    for (const std::unique_ptr<FleetShip>& s : ships)
        for (const GridPos p : s->ship.placement_order()) {
            Building* b = s->ship.building_anchored_at(p);
            const auto at = static_cast<std::size_t>(s->sector);
            if (auto* provider = dynamic_cast<Provider*>(b))
                providers[at].push_back(provider);
            else if (auto* requester = dynamic_cast<Requester*>(b); requester && requester->get_filter())
                pulled[at].push_back(*requester->get_filter());
        }
    for (int sector = 0; sector < map.size(); ++sector) {
        const std::vector<Provider*>& here = providers[static_cast<std::size_t>(sector)];
        if (here.empty())
            continue;
        std::optional<std::array<int, kItemKindCount>> room;
        for (const std::size_t d : map.deliveries_at(sector)) {
            if (!progress->open(d))
                continue;
            if (!room)
                room.emplace();
            for (const ItemStack& want : all_deliveries()[d].wants)
                (*room)[static_cast<std::size_t>(want.kind)] += progress->wanted(d, want.kind);
        }
        if (room) {
            for (const Provider* provider : here)
                for (const ItemStack& held : provider->get_stored().stacks())
                    (*room)[static_cast<std::size_t>(held.kind)] -= held.count;
            for (int& left : *room)
                left = std::max(0, left);
            // What requesters here pull, the providers take as ever.
            for (const ItemKind kind : pulled[static_cast<std::size_t>(sector)])
                (*room)[static_cast<std::size_t>(kind)] = kLinkBuffer;
        }
        for (Provider* provider : here)
            provider->limit(room);
    }
}

void Fleet::tick(Tick now, EventLog& events, StarMap* map) {
    if (map)
        limit_providers(*map);
    for (const std::unique_ptr<FleetShip>& s : ships)
        s->ship.tick(map ? &map->at(s->sector).reserves : nullptr);
    link(now);
    if (map)
        deliver(events, *map);
    std::erase_if(transfers, [&](const Transfer& t) {
        if (t.arrives > now)
            return false;
        if (FleetShip* target = find(t.to)) {
            target->ship.inventory().add(t.items);
            events.push({.kind = EventKind::CargoLanded, .ship = t.to, .sector = target->sector});
        }
        return true;
    });
    if (now % kDispatchInterval == 0)
        dispatch(now);
}
