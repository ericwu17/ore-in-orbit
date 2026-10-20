// building.cpp — item names, the inventory, footprints and the building
// factory (see building.h).
#include "sim/building.h"

#include <cassert>

const char* item_name(ItemKind kind) {
    switch (kind) {
    case ItemKind::Hull:              return "Hull";
    case ItemKind::HydrogenCollector: return "Hydrogen collector";
    case ItemKind::AsteroidCollector: return "Asteroid collector";
    case ItemKind::Pipe:              return "Pipe";
    case ItemKind::TransportBelt:     return "Transport belt";
    case ItemKind::Count:             break;
    }
    return "?";
}

void Inventory::add(ItemKind kind, int n) {
    assert(n >= 0);
    counts[static_cast<std::size_t>(kind)] += n;
}

bool Inventory::take(ItemKind kind) {
    int& n = counts[static_cast<std::size_t>(kind)];
    if (n <= 0)
        return false;
    --n;
    return true;
}

GridSize footprint_of(ItemKind kind) {
    switch (kind) {
    case ItemKind::HydrogenCollector: return HydrogenCollector::kSize;
    case ItemKind::AsteroidCollector: return AsteroidCollector::kSize;
    case ItemKind::Pipe:              return Pipe::kSize;
    case ItemKind::TransportBelt:     return TransportBelt::kSize;
    case ItemKind::Hull:
    case ItemKind::Count:             break;
    }
    return GridSize{1, 1};
}

std::unique_ptr<Building> make_building(ItemKind kind, Direction facing) {
    switch (kind) {
    case ItemKind::HydrogenCollector: return std::make_unique<HydrogenCollector>();
    case ItemKind::AsteroidCollector: return std::make_unique<AsteroidCollector>();
    case ItemKind::Pipe:              return std::make_unique<Pipe>();
    case ItemKind::TransportBelt:     return std::make_unique<TransportBelt>(facing);
    case ItemKind::Hull:
    case ItemKind::Count:             break;
    }
    assert(false && "make_building: not a building kind");
    return nullptr;
}
