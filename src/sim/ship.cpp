// ship.cpp — placement rules and the starting ship (see ship.h).
#include "sim/ship.h"

#include <algorithm>
#include <cassert>

namespace {

// What the bridge inventory starts with: some of every building (design: a
// bootstrap kit of belts, sorters, hull, a collector, crusher and furnace,
// plus one of each later building to try out), and starter cargo so
// hand-crafting has something to work with.
constexpr ItemStack kStartInventory[] = {
    {ItemKind::Hull, 150},
    // Enough for a few of the large buildings that need it.
    {ItemKind::ReinforcedHull, 60},
    {ItemKind::TransportBelt, 60},
    {ItemKind::ExpressBelt, 20},
    {ItemKind::BeltTunnel, 4},
    {ItemKind::Splitter, 2},
    {ItemKind::FilterSorter, 4},
    {ItemKind::CargoHold, 2},
    {ItemKind::Pipe, 30},
    {ItemKind::Pump, 1},
    {ItemKind::FluidTank, 1},
    {ItemKind::AsteroidCollector, 1},
    {ItemKind::TractorArray, 1},
    {ItemKind::HydrogenCollector, 1},
    {ItemKind::AsteroidCrusher, 1},
    {ItemKind::SolarFurnace, 1},
    {ItemKind::ArcFurnace, 1},
    {ItemKind::IceMelter, 1},
    {ItemKind::Electrolyzer, 1},
    {ItemKind::ChemicalPlant, 1},
    {ItemKind::FabricatorMk1, 1},
    {ItemKind::FabricatorMk2, 1},
    {ItemKind::FabricatorMk3, 1},
    {ItemKind::SolarPanel, 2},
    {ItemKind::SolarArray, 1},
    {ItemKind::FuelCellGenerator, 1},
    {ItemKind::BatteryBank, 1},
    {ItemKind::SurveyScanner, 1},
    {ItemKind::IonThruster, 1},
    {ItemKind::IronOre, 20},
    {ItemKind::Silica, 20},
    {ItemKind::IronPlate, 200},
    {ItemKind::CopperPlate, 100},
    {ItemKind::NickelPlate, 20},
    {ItemKind::Glass, 20},
    {ItemKind::Steel, 20},
};

}  // namespace

const char* describe(BuildResult result) {
    switch (result) {
    case BuildResult::Ok:               return "Ok";
    case BuildResult::OutOfBounds:      return "Outside the build grid";
    case BuildResult::NoHull:           return "Needs hull under every tile";
    case BuildResult::NeedsReinforcedHull: return "Needs reinforced hull under every tile";
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

// grid[y][x] for an in-bounds position.
template <typename Grid>
auto& at(Grid& grid, GridPos p) {
    return grid[static_cast<std::size_t>(p.y)][static_cast<std::size_t>(p.x)];
}

}  // namespace

Ship::Ship()
    : hull(kGridSize, std::vector<std::optional<ItemKind>>(kGridSize)),
      buildings(kGridSize),
      anchor(kGridSize, std::vector<std::optional<GridPos>>(kGridSize)) {
    // unique_ptr rows can't be copied from a prototype row like the others.
    for (auto& row : buildings)
        row.resize(kGridSize);
}

Ship Ship::new_game() {
    Ship ship;
    static_assert(kStartHullSide >= kBridgeSize && (kStartHullSide - kBridgeSize) % 2 == 0,
                  "the bridge sits in the middle of the starting hull");
    const int lo = (kShipGridSize - kStartHullSide) / 2;
    for (int y = lo; y < lo + kStartHullSide; ++y)
        for (int x = lo; x < lo + kStartHullSide; ++x)
            at(ship.hull, {x, y}) = ItemKind::Hull;

    auto bridge = std::make_unique<Bridge>();
    ship.bridge = bridge.get();
    ship.add_building(kStartBridgePos, std::move(bridge));

    for (const ItemStack& s : kStartInventory)
        ship.inventory().add(s.kind, s.count);
    return ship;
}

bool Ship::in_bounds(GridPos p) const {
    return p.x >= 0 && p.y >= 0 && p.x < kShipGridSize && p.y < kShipGridSize;
}

bool Ship::has_hull(GridPos p) const {
    return hull_at(p).has_value();
}

std::optional<ItemKind> Ship::hull_at(GridPos p) const {
    return in_bounds(p) ? at(hull, p) : std::nullopt;
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
    placed.push_back(p);
}

Building* Ship::building_at_mut(GridPos p) {
    const std::optional<GridPos> a = anchor_of(p);
    return a ? at(buildings, *a).get() : nullptr;
}

BuildResult Ship::can_place(ItemKind kind, GridPos p, Direction facing) const {
    assert(is_placeable(kind));
    if (!in_bounds(p))
        return BuildResult::OutOfBounds;
    if (kind == ItemKind::Hull) {
        if (has_hull(p))
            return BuildResult::HullAlreadyThere;
    } else if (kind == ItemKind::ReinforcedHull) {
        // Plain hull can be upgraded in place.
        if (hull_at(p) == ItemKind::ReinforcedHull)
            return BuildResult::HullAlreadyThere;
    } else {
        const GridSize size = footprint_of(kind, facing);
        if (!in_bounds({p.x + size.w - 1, p.y + size.h - 1}))
            return BuildResult::OutOfBounds;
        // Missing hull is reported before overlap: it's the first rule.
        for (int y = p.y; y < p.y + size.h; ++y)
            for (int x = p.x; x < p.x + size.w; ++x)
                if (!has_hull({x, y}))
                    return BuildResult::NoHull;
        if (needs_reinforced_hull(kind))
            for (int y = p.y; y < p.y + size.h; ++y)
                for (int x = p.x; x < p.x + size.w; ++x)
                    if (hull_at({x, y}) != ItemKind::ReinforcedHull)
                        return BuildResult::NeedsReinforcedHull;
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
    const BuildResult result = can_place(kind, p, facing);
    if (result != BuildResult::Ok)
        return result;
    const bool taken = inventory().take(kind);
    assert(taken);
    (void)taken;
    if (kind == ItemKind::Hull || kind == ItemKind::ReinforcedHull) {
        if (const std::optional<ItemKind> old = at(hull, p))
            inventory().add(*old);  // upgrading plain hull
        at(hull, p) = kind;
    } else
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
    inventory().add(building->contents());
    const GridSize size = building->footprint();
    for (int y = a->y; y < a->y + size.h; ++y)
        for (int x = a->x; x < a->x + size.w; ++x)
            at(anchor, {x, y}).reset();
    building.reset();
    placed.erase(std::find(placed.begin(), placed.end(), *a));
    return BuildResult::Ok;
}

BuildResult Ship::remove_hull(GridPos p) {
    if (!in_bounds(p))
        return BuildResult::OutOfBounds;
    std::optional<ItemKind>& tile = at(hull, p);
    if (!tile)
        return BuildResult::NothingThere;
    if (at(anchor, p))
        return BuildResult::HullNotEmpty;
    inventory().add(*tile);
    tile.reset();
    return BuildResult::Ok;
}

bool Ship::insert_item(GridPos to, GridPos from, ItemKind kind) {
    Building* target = building_at_mut(to);
    if (!target)
        return false;
    if (auto* sorter = dynamic_cast<Sorter*>(target))
        return sort_item(to, from, *sorter, kind);
    auto* belt = dynamic_cast<Conveyor*>(target);
    if (!belt) {
        if (!target->accepts(kind))
            return false;
        target->receive(kind);
        return true;
    }
    const Direction facing = belt->facing();
    if (from == neighbor(to, facing))
        return false;  // the cell it feeds
    int pos = 0;
    if (from != neighbor(to, rotated_cw(rotated_cw(facing)))) {
        // From the side: halfway along, unless nothing feeds it from behind.
        const auto* behind = dynamic_cast<const Conveyor*>(building_at(neighbor(to, rotated_cw(rotated_cw(facing)))));
        if (behind && behind->moves_items() && behind->facing() == facing)
            pos = kBeltLength / 2;
    }
    if (!belt->has_room_at(pos))
        return false;
    belt->insert(kind, pos);
    return true;
}

bool Ship::set_recipe(GridPos p, const Recipe* recipe) {
    auto* machine = dynamic_cast<Machine*>(building_at_mut(p));
    if (!machine || (recipe && !machine->can_run(*recipe)))
        return false;
    inventory().add(machine->set_recipe(recipe));
    return true;
}

bool Ship::sort_item(GridPos at, GridPos from, Sorter& sorter, ItemKind kind) {
    // Sorter to sorter to anything is as far as an item goes in one step
    // (Mindustry's rule): a third would let a ring of them pass it round
    // forever.
    const bool from_sorter = dynamic_cast<const Sorter*>(building_at(from)) != nullptr;
    auto pass = [&](Direction out) {
        const GridPos to = neighbor(at, out);
        if (from_sorter && dynamic_cast<const Sorter*>(building_at(to)))
            return false;
        return insert_item(to, at, kind);
    };
    const Direction d = direction_to(from, at);
    if (sorter.get_filter() == kind)
        return pass(d);
    const Direction right = rotated_cw(d);
    const Direction left = rotated_ccw(d);
    const bool first_right = sorter.next_right;
    if (pass(first_right ? right : left)) {
        sorter.next_right = !first_right;
        return true;
    }
    if (pass(first_right ? left : right)) {
        sorter.next_right = first_right;
        return true;
    }
    return false;
}

bool Ship::set_sort_filter(GridPos p, std::optional<ItemKind> item) {
    auto* sorter = dynamic_cast<Sorter*>(building_at_mut(p));
    if (!sorter)
        return false;
    sorter->set_filter(item);
    return true;
}

void Ship::push_output(GridPos p, Building& b) {
    Inventory* out = b.output_buffer();
    if (!out || out->total() == 0)
        return;
    // The cells around the footprint, clockwise from the top-left one, each
    // with the footprint cell it touches.
    struct Side {
        GridPos to;
        GridPos from;
    };
    const GridSize size = b.footprint();
    std::vector<Side> sides;
    for (int x = 0; x < size.w; ++x)
        sides.push_back({{p.x + x, p.y - 1}, {p.x + x, p.y}});
    for (int y = 0; y < size.h; ++y)
        sides.push_back({{p.x + size.w, p.y + y}, {p.x + size.w - 1, p.y + y}});
    for (int x = size.w - 1; x >= 0; --x)
        sides.push_back({{p.x + x, p.y + size.h}, {p.x + x, p.y + size.h - 1}});
    for (int y = size.h - 1; y >= 0; --y)
        sides.push_back({{p.x - 1, p.y + y}, {p.x, p.y + y}});

    const std::vector<ItemStack> held = out->stacks();
    for (std::size_t k = 0; k < sides.size(); ++k) {
        const std::size_t i = (b.next_output_side + k) % sides.size();
        for (const ItemStack& s : held)
            if (insert_item(sides[i].to, sides[i].from, s.kind)) {
                out->take(s.kind);
                b.next_output_side = i + 1;
                return;
            }
    }
}

void Ship::tick() {
    for (const GridPos p : placed)
        at(buildings, p)->tick();
    for (const GridPos p : placed) {
        auto* belt = dynamic_cast<Conveyor*>(at(buildings, p).get());
        if (!belt)
            continue;
        if (const std::optional<ItemKind> item = belt->item_at_front())
            if (insert_item(neighbor(p, belt->facing()), p, *item))
                belt->pop_front();
    }
    for (const GridPos p : placed)
        push_output(p, *at(buildings, p));
}
