// ship.cpp — placement rules and the starting ship (see ship.h).
#include "sim/ship.h"

#include <algorithm>
#include <cassert>

namespace {

// What the bridge inventory starts with (design: just enough for a first
// line: collectors feeding furnaces, belts back to the bridge, and a few
// plates to hand-craft the first gears and circuits; the rest is built).
constexpr ItemStack kStartInventory[] = {
    {ItemKind::Hull, 100},
    {ItemKind::TransportBelt, 60},
    {ItemKind::CargoHold, 1},
    {ItemKind::AsteroidCollector, 2},
    {ItemKind::SolarFurnace, 2},
    {ItemKind::FabricatorMk1, 1},
    {ItemKind::IronPlate, 20},
    {ItemKind::CopperPlate, 10},
};

// What a launched ship's bridge starts with (the starter pack's hull).
constexpr ItemStack kLaunchedInventory[] = {
    {ItemKind::Hull, 40},
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

Ship Ship::with_hull_patch(int side) {
    Ship ship;
    const int lo = (kShipGridSize - side) / 2;
    for (int y = lo; y < lo + side; ++y)
        for (int x = lo; x < lo + side; ++x)
            at(ship.hull, {x, y}) = ItemKind::Hull;

    auto bridge = std::make_unique<Bridge>();
    ship.bridge = bridge.get();
    ship.add_building(kStartBridgePos, std::move(bridge));
    return ship;
}

Ship Ship::new_game() {
    static_assert(kStartHullSide >= kBridgeSize && (kStartHullSide - kBridgeSize) % 2 == 0,
                  "the bridge sits in the middle of the starting hull");
    Ship ship = with_hull_patch(kStartHullSide);
    for (const ItemStack& s : kStartInventory)
        ship.inventory().add(s.kind, s.count);
    return ship;
}

Ship Ship::new_launched() {
    static_assert(kLaunchedHullSide >= kBridgeSize && (kLaunchedHullSide - kBridgeSize) % 2 == 0,
                  "the bridge sits in the middle of a launched ship's hull");
    Ship ship = with_hull_patch(kLaunchedHullSide);
    for (const ItemStack& s : kLaunchedInventory)
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

Building* Ship::building_anchored_at(GridPos p) {
    return in_bounds(p) ? at(buildings, p).get() : nullptr;
}

void Ship::add_building(GridPos p, std::unique_ptr<Building> building) {
    if (auto* collector = dynamic_cast<Collector*>(building.get()))
        update_yield(*collector);
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

void Ship::update_yield(Collector& collector) const {
    const std::optional<int> i = raw_resource_index(collector.get_target());
    collector.set_yield(i ? yields[static_cast<std::size_t>(*i)] : 0);
}

void Ship::set_yields(const Yields& sector_yields) {
    yields = sector_yields;
    for (const GridPos p : placed)
        if (auto* collector = dynamic_cast<Collector*>(at(buildings, p).get()))
            update_yield(*collector);
}

int Ship::hull_count() const {
    int n = 0;
    for (const auto& row : hull)
        for (const std::optional<ItemKind>& tile : row)
            n += tile ? 1 : 0;
    return n;
}

std::pair<GridPos, GridSize> Ship::hull_bounds() const {
    GridPos lo{kShipGridSize, kShipGridSize};
    GridPos hi{-1, -1};
    for (int y = 0; y < kShipGridSize; ++y)
        for (int x = 0; x < kShipGridSize; ++x)
            if (at(hull, {x, y})) {
                lo = {std::min(lo.x, x), std::min(lo.y, y)};
                hi = {std::max(hi.x, x), std::max(hi.y, y)};
            }
    if (hi.x < 0)
        return {{0, 0}, {0, 0}};
    return {lo, {hi.x - lo.x + 1, hi.y - lo.y + 1}};
}

std::vector<const Thruster*> Ship::thrusters() const {
    std::vector<const Thruster*> list;
    for (const GridPos p : placed)
        if (const auto* t = dynamic_cast<const Thruster*>(at(buildings, p).get()))
            list.push_back(t);
    return list;
}

void Ship::burn_thrusters() {
    for (const GridPos p : placed)
        if (auto* t = dynamic_cast<Thruster*>(at(buildings, p).get()))
            t->burn();
}

bool Ship::has_building(ItemKind kind) const {
    return std::any_of(placed.begin(), placed.end(),
                       [&](GridPos p) { return at(buildings, p)->item_kind() == kind; });
}

Building* Ship::building_at_mut(GridPos p) {
    const std::optional<GridPos> a = anchor_of(p);
    return a ? at(buildings, *a).get() : nullptr;
}

BuildResult Ship::can_place(ItemKind kind, GridPos p, Direction facing) const {
    return can_place(kind, p, facing, PlannedChanges{{}, {}, inventory()});
}

BuildResult Ship::can_place(ItemKind kind, GridPos p, Direction facing, const PlannedChanges& planned) const {
    assert(is_placeable(kind));
    // The grid as it will be once the planned changes are made.
    const auto hull_will_be = [&](GridPos q) -> std::optional<ItemKind> {
        if (!in_bounds(q))
            return std::nullopt;
        if (const auto it = planned.hull.find(PlannedChanges::cell_key(q)); it != planned.hull.end())
            return it->second;
        return at(hull, q);
    };
    const auto occupied = [&](GridPos q) {
        return anchor_of(q).has_value() || planned.occupied.contains(PlannedChanges::cell_key(q));
    };

    if (!in_bounds(p))
        return BuildResult::OutOfBounds;
    if (kind == ItemKind::Hull) {
        if (hull_will_be(p))
            return BuildResult::HullAlreadyThere;
    } else if (kind == ItemKind::ReinforcedHull) {
        // Plain hull can be upgraded in place.
        if (hull_will_be(p) == ItemKind::ReinforcedHull)
            return BuildResult::HullAlreadyThere;
    } else {
        const GridSize size = footprint_of(kind, facing);
        if (!in_bounds({p.x + size.w - 1, p.y + size.h - 1}))
            return BuildResult::OutOfBounds;
        // Missing hull is reported before overlap: it's the first rule.
        for (int y = p.y; y < p.y + size.h; ++y)
            for (int x = p.x; x < p.x + size.w; ++x)
                if (!hull_will_be({x, y}))
                    return BuildResult::NoHull;
        if (needs_reinforced_hull(kind))
            for (int y = p.y; y < p.y + size.h; ++y)
                for (int x = p.x; x < p.x + size.w; ++x)
                    if (hull_will_be({x, y}) != ItemKind::ReinforcedHull)
                        return BuildResult::NeedsReinforcedHull;
        for (int y = p.y; y < p.y + size.h; ++y)
            for (int x = p.x; x < p.x + size.w; ++x)
                if (occupied({x, y}))
                    return BuildResult::Occupied;
    }
    if (planned.inventory.count(kind) <= 0)
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

std::unique_ptr<Building> Ship::take_building(GridPos a) {
    std::unique_ptr<Building> building = std::move(at(buildings, a));
    assert(building);
    const GridSize size = building->footprint();
    for (int y = a.y; y < a.y + size.h; ++y)
        for (int x = a.x; x < a.x + size.w; ++x)
            at(anchor, {x, y}).reset();
    placed.erase(std::find(placed.begin(), placed.end(), a));
    return building;
}

BuildResult Ship::remove_building(GridPos p) {
    if (!in_bounds(p))
        return BuildResult::OutOfBounds;
    const std::optional<GridPos> a = at(anchor, p);
    if (!a)
        return BuildResult::NothingThere;
    if (!at(buildings, *a)->removable())
        return BuildResult::NotRemovable;
    const std::unique_ptr<Building> building = take_building(*a);
    if (const auto kind = building->item_kind())
        inventory().add(*kind);
    inventory().add(building->contents());
    return BuildResult::Ok;
}

void Ship::destroy_building(GridPos a) {
    assert(at(buildings, a).get() != bridge);
    take_building(a);
}

void Ship::repair_all(int percent) {
    for (const GridPos p : placed) {
        Building& b = *at(buildings, p);
        if (b.damaged())
            b.repair(std::max(1, b.max_hp() * percent / 100));
    }
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
    return insert_item(to, from, building_at(from), kind);
}

bool Ship::insert_item(GridPos to, GridPos from, const Building* source, ItemKind kind) {
    Building* target = building_at_mut(to);
    if (!target)
        return false;
    if (auto* sorter = dynamic_cast<Sorter*>(target))
        return sort_item(to, from, source, *sorter, kind);
    if (auto* junction = dynamic_cast<Junction*>(target)) {
        const Direction travel = direction_to(from, to);
        if (!junction->has_room(travel) || !building_at(neighbor(to, travel)))
            return false;  // full that way, or nothing to cross to
        junction->insert(kind, travel);
        return true;
    }
    if (auto* router = dynamic_cast<Router*>(target)) {
        if (!router->empty())
            return false;
        router->insert(kind, direction_to(to, from));
        return true;
    }
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
    if (source && building_at(neighbor(to, facing)) == source)
        return false;  // back where it came from, through a sorter
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
    if (!machine || (recipe && (!machine->can_run(*recipe) || !bridge->get_progress().unlocked(*recipe))))
        return false;
    inventory().add(machine->set_recipe(recipe));
    return true;
}

bool Ship::sort_item(GridPos at, GridPos from, const Building* source, Sorter& sorter, ItemKind kind) {
    // Sorter to sorter to anything is as far as an item goes in one step
    // (Mindustry's rule): a third would let a ring of them pass it round
    // forever.
    const bool from_sorter = dynamic_cast<const Sorter*>(building_at(from)) != nullptr;
    auto pass = [&](Direction out) {
        const GridPos to = neighbor(at, out);
        if (from_sorter && dynamic_cast<const Sorter*>(building_at(to)))
            return false;
        return insert_item(to, at, source, kind);
    };
    const Direction d = direction_to(from, at);
    if (sorter.passes_straight(kind))
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

bool Ship::set_collector_target(GridPos p, ItemKind target) {
    auto* collector = dynamic_cast<Collector*>(building_at_mut(p));
    if (!collector || !collector->can_target(target))
        return false;
    collector->set_target(target);
    update_yield(*collector);
    return true;
}

void Ship::pass_on(GridPos p, Building& b) {
    if (auto* junction = dynamic_cast<Junction*>(&b)) {
        for (const Direction travel : {Direction::North, Direction::East, Direction::South, Direction::West})
            if (const std::optional<ItemKind> item = junction->item_ready(travel))
                if (insert_item(neighbor(p, travel), p, *item))
                    junction->pop(travel);
    } else if (auto* router = dynamic_cast<Router*>(&b)) {
        const std::optional<ItemKind> item = router->item_ready();
        if (!item)
            return;
        // Clockwise from the side whose turn it is, skipping the way back.
        Direction out = router->next_out;
        for (int k = 0; k < 4; ++k, out = rotated_cw(out)) {
            if (out == router->get_came_from())
                continue;
            if (insert_item(neighbor(p, out), p, *item)) {
                router->pop();
                router->next_out = rotated_cw(out);
                return;
            }
        }
    }
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
    for (const GridPos p : placed)
        pass_on(p, *at(buildings, p));
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
