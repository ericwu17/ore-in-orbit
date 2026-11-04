// ship.cpp — placement rules, the starting ship and moving items between
// buildings (see ship.h).
#include "sim/ship.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>

namespace {

// What the bridge inventory starts with (design: just enough for a first
// line: collectors feeding furnaces, tubes back to the bridge, and a few
// plates to hand-craft the first gears and circuits; the rest is built).
constexpr ItemStack kStartInventory[] = {
    {ItemKind::Tube, 60},
    {ItemKind::CheckValve, 4},
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
    const bool vacuum = dynamic_cast<const VacuumLine*>(building.get()) != nullptr;
    at(buildings, p) = std::move(building);
    placed.push_back(p);
    if (vacuum)
        link_vacuum_lines();
}

std::optional<GridPos> Ship::vacuum_along(GridPos p, Direction way, Direction facing) const {
    for (int i = 0; i < kVacuumRange; ++i) {
        p = neighbor(p, way);
        const auto* line = dynamic_cast<const VacuumLine*>(building_at(p));
        if (line && line->facing() == facing)
            return p;
    }
    return std::nullopt;
}

void Ship::link_vacuum_lines() {
    std::vector<GridPos> lines;
    for (const GridPos p : placed)
        if (auto* line = dynamic_cast<VacuumLine*>(at(buildings, p).get())) {
            line->unlink();
            lines.push_back(p);
        }
    // Rearmost first: how far along its own direction each one stands.
    const auto along = [&](GridPos p) {
        const GridPos step = neighbor({0, 0}, at(buildings, p)->facing());
        return p.x * step.x + p.y * step.y;
    };
    std::sort(lines.begin(), lines.end(), [&](GridPos a, GridPos b) { return along(a) < along(b); });
    for (const GridPos p : lines) {
        auto& entrance = dynamic_cast<VacuumLine&>(*at(buildings, p));
        if (entrance.is_exit())
            continue;
        if (const std::optional<GridPos> ahead = vacuum_along(p, entrance.facing(), entrance.facing())) {
            const int cells = std::abs(ahead->x - p.x) + std::abs(ahead->y - p.y);
            entrance.link_exit(*ahead, cells);
            dynamic_cast<VacuumLine&>(*at(buildings, *ahead)).link_entrance(p, cells);
        }
    }
}

VacuumPreview Ship::vacuum_preview(GridPos p, Direction facing) const {
    if (const std::optional<GridPos> behind = vacuum_along(p, opposite(facing), facing))
        if (!dynamic_cast<const VacuumLine&>(*building_at(*behind)).is_exit())
            return {behind, true};
    return {vacuum_along(p, facing, facing), false};
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
    if (dynamic_cast<const VacuumLine*>(building.get()))
        link_vacuum_lines();
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
    Building* target = building_at_mut(to);
    if (!target)
        return false;
    if (auto* tube = dynamic_cast<Tube*>(target))
        return insert_into_tube(to, from, *tube, kind);
    if (auto* crossover = dynamic_cast<Crossover*>(target)) {
        const Direction travel = direction_to(from, to);
        if (!crossover->has_room(travel) || !building_at(neighbor(to, travel)))
            return false;  // full that way, or nothing to cross to
        crossover->insert(kind, travel);
        return true;
    }
    if (auto* line = dynamic_cast<VacuumLine*>(target)) {
        // An exit is fed by its entrance alone, and nothing goes in by the
        // front.
        if (line->is_exit() || from == neighbor(to, line->facing()) || !line->has_room())
            return false;
        line->insert(kind);
        return true;
    }
    if (!target->accepts(kind))
        return false;
    target->receive(kind);
    return true;
}

bool Ship::tube_links(GridPos p, Direction side) const {
    const auto* tube = dynamic_cast<const Tube*>(building_at(p));
    if (!tube)
        return false;
    if (tube->is_valve() && side != tube->facing() && side != opposite(tube->facing()))
        return false;
    const GridPos q = neighbor(p, side);
    const Building* b = building_at(q);
    if (!b)
        return false;
    // The way from the neighbor back to this tube.
    const Direction back = opposite(side);
    if (const auto* other = dynamic_cast<const Tube*>(b)) {
        if (other->get_color() != tube->get_color())
            return false;
        return !other->is_valve() || back == other->facing() || back == opposite(other->facing());
    }
    if (const auto* extractor = dynamic_cast<const Extractor*>(b))
        return back == extractor->facing();
    if (const auto* line = dynamic_cast<const VacuumLine*>(b))
        return line->is_exit() ? back == line->facing() : back != line->facing();
    return true;
}

Ship::TubeExit Ship::tube_exit(GridPos p, const Tube& tube, Direction side, ItemKind kind) const {
    if (!tube_links(p, side) || (tube.is_valve() && side != tube.facing()))
        return TubeExit::None;
    const GridPos q = neighbor(p, side);
    const Building* b = building_at(q);
    if (const auto* other = dynamic_cast<const Tube*>(b)) {
        if (other->is_valve() && neighbor(q, opposite(other->facing())) != p)
            return TubeExit::None;  // a valve takes items only from behind
        if (other->is_filter())
            return other->admits(kind) ? TubeExit::Building : TubeExit::None;
        return other->is_overflow() ? TubeExit::Overflow : TubeExit::Tube;
    }
    if (dynamic_cast<const Crossover*>(b) || dynamic_cast<const VacuumLine*>(b))
        return TubeExit::Tube;
    if (dynamic_cast<const Extractor*>(b))
        return TubeExit::None;  // it only gives
    return TubeExit::Building;
}

bool Ship::takes_now(GridPos p, ItemKind kind) const {
    const Building* b = building_at(p);
    if (const auto* tube = dynamic_cast<const Tube*>(b))
        return tube->admits(kind) && tube->has_room();
    return b && b->accepts(kind);
}

bool Ship::tube_leads_on(GridPos p, const Tube& tube, Direction from, ItemKind kind) const {
    for (int i = 0; i < 4; ++i) {
        const auto d = static_cast<Direction>(i);
        if (d == from)
            continue;
        const TubeExit exit = tube_exit(p, tube, d, kind);
        // A filter tube counts among the buildings for rule 3, but it's tube.
        if (exit == TubeExit::Tube || exit == TubeExit::Overflow ||
            (exit == TubeExit::Building && dynamic_cast<const Tube*>(building_at(neighbor(p, d)))))
            return true;
    }
    return false;
}

Direction Ship::plan_tube_exit(GridPos p, Tube& tube, Direction from, ItemKind kind) {
    // The sides in turn, from the one whose turn it is.
    const auto side = [&](int k) { return static_cast<Direction>((tube.next_out + k) % 4); };
    for (int k = 0; k < 4; ++k) {
        const Direction d = side(k);
        if (d != from && tube_exit(p, tube, d, kind) == TubeExit::Building && takes_now(neighbor(p, d), kind))
            return d;
    }
    for (const TubeExit want : {TubeExit::Tube, TubeExit::Overflow})
        for (int k = 0; k < 4; ++k) {
            const Direction d = side(k);
            if (d != from && tube_exit(p, tube, d, kind) == want) {
                tube.next_out = (static_cast<int>(d) + 1) % 4;
                return d;
            }
        }
    return tube_leads_on(p, tube, from, kind) ? opposite(from) : from;
}

bool Ship::insert_into_tube(GridPos to, GridPos from, Tube& tube, ItemKind kind) {
    const Direction side = direction_to(to, from);
    if (!tube_links(to, side) || (tube.is_valve() && side != opposite(tube.facing())))
        return false;
    if (!tube.admits(kind) || !tube.has_room())
        return false;
    tube.insert(kind, side, plan_tube_exit(to, tube, side, kind));
    return true;
}

void Ship::move_tube_item(GridPos p, Tube& tube) {
    const TubeItem* front = tube.item_at_front();
    if (!front) {
        tube.waited = 0;
        return;
    }
    const TubeItem item = *front;
    for (const TubeExit want : {TubeExit::Building, TubeExit::Tube, TubeExit::Overflow}) {
        // The side it was headed for first, then the others clockwise.
        Direction d = item.to;
        for (int k = 0; k < 4; ++k, d = rotated_cw(d)) {
            if (d == item.from || tube_exit(p, tube, d, item.kind) != want)
                continue;
            if (insert_item(neighbor(p, d), p, item.kind)) {
                tube.pop_front();
                tube.waited = 0;
                return;
            }
        }
    }
    // Where the tubes end, it turns round.
    if (!tube_leads_on(p, tube, item.from, item.kind) && insert_item(neighbor(p, item.from), p, item.kind)) {
        tube.pop_front();
        tube.waited = 0;
        return;
    }
    ++tube.waited;
}

bool Ship::set_recipe(GridPos p, const Recipe* recipe) {
    auto* machine = dynamic_cast<Machine*>(building_at_mut(p));
    if (!machine || (recipe && (!machine->can_run(*recipe) || !bridge->get_progress().unlocked(*recipe))))
        return false;
    inventory().add(machine->set_recipe(recipe));
    return true;
}

bool Ship::set_filter(GridPos p, std::optional<ItemKind> item) {
    Building* b = building_at_mut(p);
    if (auto* tube = dynamic_cast<Tube*>(b); tube && tube->is_filter())
        tube->set_filter(item);
    else if (auto* extractor = dynamic_cast<Extractor*>(b))
        extractor->set_filter(item);
    else
        return false;
    return true;
}

bool Ship::set_tube_color(GridPos p, TubeColor color) {
    auto* tube = dynamic_cast<Tube*>(building_at_mut(p));
    if (!tube)
        return false;
    tube->set_color(color);
    return true;
}

const Building* Ship::extract_source(GridPos p) const {
    const auto* extractor = dynamic_cast<const Extractor*>(building_at(p));
    if (!extractor)
        return nullptr;
    const Building* source = building_at(neighbor(p, opposite(extractor->facing())));
    return source && source->storage() ? source : nullptr;
}

bool Ship::set_collector_target(GridPos p, ItemKind target) {
    auto* collector = dynamic_cast<Collector*>(building_at_mut(p));
    if (!collector || !collector->can_target(target))
        return false;
    collector->set_target(target);
    update_yield(*collector);
    return true;
}

bool Ship::set_only_when_requested(GridPos p, bool on) {
    Building* b = building_at_mut(p);
    if (auto* collector = dynamic_cast<Collector*>(b))
        collector->set_only_when_requested(on);
    else if (auto* machine = dynamic_cast<Machine*>(b))
        machine->set_only_when_requested(on);
    else
        return false;
    return true;
}

void Ship::pass_on(GridPos p, Crossover& crossover) {
    for (const Direction travel : {Direction::North, Direction::East, Direction::South, Direction::West})
        if (const std::optional<ItemKind> item = crossover.item_ready(travel))
            if (insert_item(neighbor(p, travel), p, *item))
                crossover.pop(travel);
}

void Ship::move_vacuum_item(GridPos p, VacuumLine& line) {
    const std::optional<ItemKind> item = line.item_at_front();
    if (!item)
        return;
    if (line.is_exit()) {
        if (insert_item(neighbor(p, line.facing()), p, *item))
            line.pop_front();
    } else if (const std::optional<GridPos> exit = line.get_exit()) {
        // Straight to its exit; with none, it holds on to it.
        auto& out = dynamic_cast<VacuumLine&>(*at(buildings, *exit));
        if (out.has_room()) {
            out.insert(*item);
            line.pop_front();
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

    const auto overflow = [&](GridPos q) {
        const auto* tube = dynamic_cast<const Tube*>(building_at(q));
        return tube && tube->is_overflow();
    };
    const std::vector<ItemStack> held = out->stacks();
    // Overflow tubes only once nothing else takes it.
    for (const bool to_overflow : {false, true})
        for (std::size_t k = 0; k < sides.size(); ++k) {
            const std::size_t i = (b.next_output_side + k) % sides.size();
            if (overflow(sides[i].to) != to_overflow)
                continue;
            for (const ItemStack& s : held)
                if (insert_item(sides[i].to, sides[i].from, s.kind)) {
                    out->take(s.kind);
                    b.next_output_side = i + 1;
                    return;
                }
        }
}

void Ship::extract(GridPos p, Extractor& extractor) {
    if (!extractor.ready() || !extract_source(p))
        return;
    Building& source = *building_at_mut(neighbor(p, opposite(extractor.facing())));
    const Inventory& stored = *source.storage();
    const GridPos front = neighbor(p, extractor.facing());
    const auto take = [&](ItemKind kind) {
        if (stored.count(kind) <= 0 || !insert_item(front, p, kind))
            return false;
        source.unload(kind);
        extractor.extracted(kind);
        return true;
    };
    if (const std::optional<ItemKind> filter = extractor.get_filter()) {
        take(*filter);
        return;
    }
    // Every kind in turn, from the one after the last taken.
    constexpr auto kinds = static_cast<std::size_t>(kItemKindCount);
    for (std::size_t k = 0; k < kinds; ++k)
        if (take(static_cast<ItemKind>((extractor.next_kind + k) % kinds)))
            return;
}

void Ship::tick() {
    for (const GridPos p : placed) {
        Building& b = *at(buildings, p);
        const auto standby = [&](bool only_when_requested) {
            const std::optional<ItemKind> product = product_of(b);
            return only_when_requested && !(product && bridge->request_unmet(*product));
        };
        if (auto* collector = dynamic_cast<Collector*>(&b))
            collector->set_standby(standby(collector->get_only_when_requested()));
        else if (auto* machine = dynamic_cast<Machine*>(&b))
            machine->set_standby(standby(machine->get_only_when_requested()));
    }
    for (const GridPos p : placed)
        at(buildings, p)->tick();
    for (const GridPos p : placed)
        if (auto* crossover = dynamic_cast<Crossover*>(at(buildings, p).get()))
            pass_on(p, *crossover);
    for (const GridPos p : placed)
        if (auto* line = dynamic_cast<VacuumLine*>(at(buildings, p).get()))
            move_vacuum_item(p, *line);
    for (const GridPos p : placed)
        if (auto* tube = dynamic_cast<Tube*>(at(buildings, p).get()))
            move_tube_item(p, *tube);
    for (const GridPos p : placed) {
        Building& b = *at(buildings, p);
        push_output(p, b);
        if (auto* extractor = dynamic_cast<Extractor*>(&b))
            extract(p, *extractor);
    }
}
