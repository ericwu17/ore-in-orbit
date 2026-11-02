// ship.cpp — placement rules, the starting ship and moving items between
// buildings (see ship.h).
#include "sim/ship.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>

namespace {

// What the bridge inventory starts with (design: just enough for a first
// line: collectors feeding furnaces, belts back to the bridge, and a few
// plates to hand-craft the first gears and circuits; the rest is built).
constexpr ItemStack kStartInventory[] = {
    {ItemKind::TransportBelt, 60},
    {ItemKind::CargoHold, 1},
    {ItemKind::AsteroidCollector, 2},
    {ItemKind::SolarFurnace, 2},
    {ItemKind::FabricatorMk1, 1},
    {ItemKind::IronPlate, 20},
    {ItemKind::CopperPlate, 10},
};

}  // namespace

const char* describe(BuildResult result) {
    switch (result) {
    case BuildResult::Ok:               return "Ok";
    case BuildResult::OutOfBounds:      return "Outside the build grid";
    case BuildResult::NoHull:           return "Needs hull under every tile";
    case BuildResult::Occupied:         return "Something is already built there";
    case BuildResult::NoneInInventory:  return "None left in the bridge inventory";
    case BuildResult::NothingThere:     return "Nothing to remove there";
    case BuildResult::NotRemovable:     return "Part of the ship's layout: it can't be removed";
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
    : hull(kGridSize, std::vector<char>(kGridSize)),
      buildings(kGridSize),
      anchor(kGridSize, std::vector<std::optional<GridPos>>(kGridSize)) {
    // unique_ptr rows can't be copied from a prototype row like the others.
    for (auto& row : buildings)
        row.resize(kGridSize);
}

void Ship::set_hull(const ShipLayout& ship_layout) {
    layout = &ship_layout;
    const GridPos origin = layout_origin(ship_layout);
    const GridSize size = layout_bounds(ship_layout);
    for (int y = 0; y < size.h; ++y)
        for (int x = 0; x < size.w; ++x)
            if (layout_has_hull(ship_layout, {x, y}))
                at(hull, {origin.x + x, origin.y + y}) = 1;
}

Ship Ship::from_layout(const ShipLayout& ship_layout) {
    Ship ship;
    ship.set_hull(ship_layout);
    auto bridge = std::make_unique<Bridge>();
    ship.bridge = bridge.get();
    ship.add_building(layout_to_grid(ship_layout, ship_layout.bridge), std::move(bridge));
    for (const GridPos e : ship_layout.engines)
        ship.add_building(layout_to_grid(ship_layout, e), make_building(ItemKind::IonThruster, Direction::North));
    return ship;
}

Ship Ship::new_game() {
    Ship ship = from_layout(starter_layout());
    for (const ItemStack& s : kStartInventory)
        ship.inventory().add(s.kind, s.count);
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
    const bool tunnel = dynamic_cast<const Tunnel*>(building.get()) != nullptr;
    at(buildings, p) = std::move(building);
    placed.push_back(p);
    if (tunnel)
        link_tunnels();
}

std::optional<GridPos> Ship::tunnel_along(GridPos p, Direction way, Direction facing) const {
    for (int i = 0; i < kTunnelRange; ++i) {
        p = neighbor(p, way);
        const auto* tunnel = dynamic_cast<const Tunnel*>(building_at(p));
        if (tunnel && tunnel->facing() == facing)
            return p;
    }
    return std::nullopt;
}

void Ship::link_tunnels() {
    std::vector<GridPos> tunnels;
    for (const GridPos p : placed)
        if (auto* tunnel = dynamic_cast<Tunnel*>(at(buildings, p).get())) {
            tunnel->unlink();
            tunnels.push_back(p);
        }
    // Rearmost first: how far along its own direction each one stands.
    const auto along = [&](GridPos p) {
        const GridPos step = neighbor({0, 0}, at(buildings, p)->facing());
        return p.x * step.x + p.y * step.y;
    };
    std::sort(tunnels.begin(), tunnels.end(), [&](GridPos a, GridPos b) { return along(a) < along(b); });
    for (const GridPos p : tunnels) {
        auto& entrance = dynamic_cast<Tunnel&>(*at(buildings, p));
        if (entrance.is_exit())
            continue;
        if (const std::optional<GridPos> ahead = tunnel_along(p, entrance.facing(), entrance.facing())) {
            const int cells = std::abs(ahead->x - p.x) + std::abs(ahead->y - p.y);
            entrance.link_exit(*ahead, cells);
            dynamic_cast<Tunnel&>(*at(buildings, *ahead)).link_entrance(p, cells);
        }
    }
}

TunnelPreview Ship::tunnel_preview(GridPos p, Direction facing) const {
    const Direction back = rotated_cw(rotated_cw(facing));
    if (const std::optional<GridPos> behind = tunnel_along(p, back, facing))
        if (!dynamic_cast<const Tunnel&>(*building_at(*behind)).is_exit())
            return {behind, true};
    return {tunnel_along(p, facing, facing), false};
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
        for (const char tile : row)
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
    return can_place(kind, p, facing, PlannedChanges{{}, inventory()});
}

BuildResult Ship::can_place(ItemKind kind, GridPos p, Direction facing, const PlannedChanges& planned) const {
    assert(is_placeable(kind));
    // The grid as it will be once the planned changes are made.
    const auto occupied = [&](GridPos q) {
        return anchor_of(q).has_value() || planned.occupied.contains(PlannedChanges::cell_key(q));
    };

    const GridSize size = footprint_of(kind, facing);
    if (!in_bounds(p) || !in_bounds({p.x + size.w - 1, p.y + size.h - 1}))
        return BuildResult::OutOfBounds;
    // Missing hull is reported before overlap: it's the first rule.
    for (int y = p.y; y < p.y + size.h; ++y)
        for (int x = p.x; x < p.x + size.w; ++x)
            if (!has_hull({x, y}))
                return BuildResult::NoHull;
    for (int y = p.y; y < p.y + size.h; ++y)
        for (int x = p.x; x < p.x + size.w; ++x)
            if (occupied({x, y}))
                return BuildResult::Occupied;
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
    if (dynamic_cast<const Tunnel*>(building.get()))
        link_tunnels();
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
    assert(at(buildings, a)->removable());
    take_building(a);
}

void Ship::repair_all(int percent) {
    for (const GridPos p : placed) {
        Building& b = *at(buildings, p);
        if (b.damaged())
            b.repair(std::max(1, b.max_hp() * percent / 100));
    }
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
    const GridPos back = neighbor(to, rotated_cw(rotated_cw(facing)));
    const auto* tunnel = dynamic_cast<const Tunnel*>(belt);
    const bool exit = tunnel && tunnel->is_exit();
    if (exit && from == back)
        return false;  // the tunnel's mouth
    int pos = 0;
    if (from != back) {
        // From the side: halfway along, unless nothing feeds it from behind.
        const auto* behind = dynamic_cast<const Conveyor*>(building_at(back));
        if (exit || (behind && behind->facing() == facing))
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
        const std::optional<ItemKind> item = belt->item_at_front();
        if (!item)
            continue;
        if (const auto* tunnel = dynamic_cast<const Tunnel*>(belt); tunnel && !tunnel->is_exit()) {
            // Straight to its exit; with none, it holds on to it.
            if (const std::optional<GridPos> exit = tunnel->get_exit()) {
                auto& out = dynamic_cast<Tunnel&>(*at(buildings, *exit));
                if (out.has_room_at(0)) {
                    out.insert(*item, 0);
                    belt->pop_front();
                }
            }
        } else if (insert_item(neighbor(p, belt->facing()), p, *item)) {
            belt->pop_front();
        }
    }
    for (const GridPos p : placed)
        push_output(p, *at(buildings, p));
}
