// ship.cpp — placement rules and the starting ship (see ship.h).
#include "sim/ship.h"

#include <cassert>

namespace {

// The starting ship: a square hull patch of this side, centered on the grid,
// with the bridge at its center.
constexpr int kStartHullSide = 5;

// What the bridge inventory starts with.
constexpr int kStartHull              = 150;
constexpr int kStartHydrogenCollector = 8;
constexpr int kStartAsteroidCollector = 8;
constexpr int kStartPipe              = 60;
constexpr int kStartTransportBelt     = 60;

}  // namespace

const char* describe(BuildResult result) {
    switch (result) {
    case BuildResult::Ok:               return "Ok";
    case BuildResult::OutOfBounds:      return "Outside the build grid";
    case BuildResult::NoHull:           return "Needs hull under every tile";
    case BuildResult::HullAlreadyThere: return "Already hull there";
    case BuildResult::Occupied:         return "Something is already built there";
    case BuildResult::NoneInInventory:  return "None left in the bridge inventory";
    case BuildResult::NothingThere:     return "Nothing to remove there";
    case BuildResult::NotRemovable:     return "The bridge can't be removed";
    case BuildResult::HullNotEmpty:     return "Remove the building on it first";
    }
    return "?";
}

namespace {

constexpr std::size_t kGridSize = static_cast<std::size_t>(kShipGridSize);

// grid[y][x] for an in-bounds position. decltype(auto): vector<bool> hands
// back a proxy, not a reference.
template <typename Grid>
decltype(auto) at(Grid& grid, GridPos p) {
    return grid[static_cast<std::size_t>(p.y)][static_cast<std::size_t>(p.x)];
}

}  // namespace

Ship::Ship()
    : hull(kGridSize, std::vector<bool>(kGridSize, false)),
      buildings(kGridSize),
      anchor(kGridSize, std::vector<std::optional<GridPos>>(kGridSize)) {
    // unique_ptr rows can't be copied from a prototype row like the others.
    for (auto& row : buildings)
        row.resize(kGridSize);
}

Ship Ship::new_game() {
    Ship ship;
    const int center = kShipGridSize / 2;
    const int lo = center - kStartHullSide / 2;
    for (int y = lo; y < lo + kStartHullSide; ++y)
        for (int x = lo; x < lo + kStartHullSide; ++x)
            at(ship.hull, {x, y}) = true;

    auto bridge = std::make_unique<Bridge>();
    ship.bridge = bridge.get();
    ship.add_building({center, center}, std::move(bridge));

    Inventory& inv = ship.inventory();
    inv.add(ItemKind::Hull, kStartHull);
    inv.add(ItemKind::HydrogenCollector, kStartHydrogenCollector);
    inv.add(ItemKind::AsteroidCollector, kStartAsteroidCollector);
    inv.add(ItemKind::Pipe, kStartPipe);
    inv.add(ItemKind::TransportBelt, kStartTransportBelt);
    return ship;
}

bool Ship::in_bounds(GridPos p) const {
    return p.x >= 0 && p.y >= 0 && p.x < kShipGridSize && p.y < kShipGridSize;
}

bool Ship::has_hull(GridPos p) const {
    return in_bounds(p) && at(hull, p);
}

std::optional<GridPos> Ship::anchor_of(GridPos p) const {
    return in_bounds(p) ? at(anchor, p) : std::nullopt;
}

const Building* Ship::building_at(GridPos p) const {
    const std::optional<GridPos> a = anchor_of(p);
    return a ? at(buildings, *a).get() : nullptr;
}

const Building* Ship::building_anchored_at(GridPos p) const {
    return in_bounds(p) ? at(buildings, p).get() : nullptr;
}

void Ship::add_building(GridPos p, std::unique_ptr<Building> building) {
    const GridSize size = building->footprint();
    for (int y = p.y; y < p.y + size.h; ++y)
        for (int x = p.x; x < p.x + size.w; ++x) {
            std::optional<GridPos>& cell = at(anchor, {x, y});
            assert(!cell);
            cell = p;
        }
    at(buildings, p) = std::move(building);
}

BuildResult Ship::can_place(ItemKind kind, GridPos p) const {
    if (!in_bounds(p))
        return BuildResult::OutOfBounds;
    if (kind == ItemKind::Hull) {
        if (has_hull(p))
            return BuildResult::HullAlreadyThere;
    } else {
        const GridSize size = footprint_of(kind);
        if (!in_bounds({p.x + size.w - 1, p.y + size.h - 1}))
            return BuildResult::OutOfBounds;
        // Missing hull is reported before overlap: it's the first rule.
        for (int y = p.y; y < p.y + size.h; ++y)
            for (int x = p.x; x < p.x + size.w; ++x)
                if (!has_hull({x, y}))
                    return BuildResult::NoHull;
        for (int y = p.y; y < p.y + size.h; ++y)
            for (int x = p.x; x < p.x + size.w; ++x)
                if (anchor_of({x, y}))
                    return BuildResult::Occupied;
    }
    if (inventory().count(kind) <= 0)
        return BuildResult::NoneInInventory;
    return BuildResult::Ok;
}

BuildResult Ship::place(ItemKind kind, GridPos p, Direction facing) {
    const BuildResult result = can_place(kind, p);
    if (result != BuildResult::Ok)
        return result;
    const bool taken = inventory().take(kind);
    assert(taken);
    (void)taken;
    if (kind == ItemKind::Hull)
        at(hull, p) = true;
    else
        add_building(p, make_building(kind, facing));
    return BuildResult::Ok;
}

BuildResult Ship::remove_building(GridPos p) {
    if (!in_bounds(p))
        return BuildResult::OutOfBounds;
    const std::optional<GridPos> a = at(anchor, p);
    if (!a)
        return BuildResult::NothingThere;
    std::unique_ptr<Building>& building = at(buildings, *a);
    if (!building->removable())
        return BuildResult::NotRemovable;
    if (const auto kind = building->item_kind())
        inventory().add(*kind);
    const GridSize size = building->footprint();
    for (int y = a->y; y < a->y + size.h; ++y)
        for (int x = a->x; x < a->x + size.w; ++x)
            at(anchor, {x, y}).reset();
    building.reset();
    return BuildResult::Ok;
}

BuildResult Ship::remove_hull(GridPos p) {
    if (!in_bounds(p))
        return BuildResult::OutOfBounds;
    if (!at(hull, p))
        return BuildResult::NothingThere;
    if (at(anchor, p))
        return BuildResult::HullNotEmpty;
    at(hull, p) = false;
    inventory().add(ItemKind::Hull);
    return BuildResult::Ok;
}

void Ship::tick() {
    for (const auto& row : buildings)
        for (const auto& building : row)
            if (building)
                building->tick();
}
