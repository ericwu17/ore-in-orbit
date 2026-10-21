// blueprint.cpp — copying, turning and pasting blueprints (see blueprint.h).
#include "sim/blueprint.h"

#include "sim/building.h"

#include <algorithm>
#include <cassert>

namespace {

// Whether two rectangles, each given by its top-left cell and size, share a
// cell.
bool overlaps(GridPos a, GridSize as, GridPos b, GridSize bs) {
    return a.x < b.x + bs.w && b.x < a.x + as.w && a.y < b.y + bs.h && b.y < a.y + as.h;
}

bool contains(GridPos min, GridPos max, GridPos p) {
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
}

}  // namespace

std::optional<Blueprint> copy_region(const Ship& ship, GridPos a, GridPos b) {
    const int last = ship.size() - 1;
    const GridPos lo{std::clamp(std::min(a.x, b.x), 0, last), std::clamp(std::min(a.y, b.y), 0, last)};
    const GridPos hi{std::clamp(std::max(a.x, b.x), 0, last), std::clamp(std::max(a.y, b.y), 0, last)};
    const GridSize box{hi.x - lo.x + 1, hi.y - lo.y + 1};

    // The buildings touching the box (never the bridge), and the cells
    // whose hull comes along: the box's, and those under those buildings.
    struct Taken {
        GridPos at;
        const Building* building;
    };
    std::vector<Taken> taken;
    for (const GridPos p : ship.placement_order()) {
        const Building* building = ship.building_anchored_at(p);
        if (building->item_kind() && overlaps(p, building->footprint(), lo, box))
            taken.push_back({p, building});
    }
    const auto hull_comes = [&](GridPos p) {
        if (contains(lo, hi, p))
            return true;
        const std::optional<GridPos> anchor = ship.anchor_of(p);
        return anchor && std::any_of(taken.begin(), taken.end(), [&](const Taken& t) { return t.at == *anchor; });
    };

    // Trimmed to what is taken.
    GridPos min{ship.size(), ship.size()};
    GridPos max{-1, -1};
    const auto include = [&](GridPos p) {
        min = {std::min(min.x, p.x), std::min(min.y, p.y)};
        max = {std::max(max.x, p.x), std::max(max.y, p.y)};
    };
    for (const Taken& t : taken) {
        const GridSize size = t.building->footprint();
        include(t.at);
        include({t.at.x + size.w - 1, t.at.y + size.h - 1});
    }
    for (int y = lo.y; y <= hi.y; ++y)
        for (int x = lo.x; x <= hi.x; ++x)
            if (ship.has_hull({x, y}))
                include({x, y});
    if (max.x < 0)
        return std::nullopt;

    Blueprint blueprint;
    blueprint.size = {max.x - min.x + 1, max.y - min.y + 1};
    blueprint.hull.resize(static_cast<std::size_t>(blueprint.size.w * blueprint.size.h));
    for (int y = min.y; y <= max.y; ++y)
        for (int x = min.x; x <= max.x; ++x)
            if (hull_comes({x, y}))
                blueprint.hull[static_cast<std::size_t>((y - min.y) * blueprint.size.w + (x - min.x))] =
                    ship.hull_at({x, y});
    for (const Taken& t : taken) {
        BlueprintBuilding copy;
        copy.kind = *t.building->item_kind();
        copy.offset = {t.at.x - min.x, t.at.y - min.y};
        copy.facing = t.building->facing();
        if (const auto* machine = dynamic_cast<const Machine*>(t.building))
            copy.recipe = machine->get_recipe();
        if (const auto* collector = dynamic_cast<const Collector*>(t.building))
            copy.target = collector->get_target();
        if (const auto* sorter = dynamic_cast<const Sorter*>(t.building))
            copy.filter = sorter->get_filter();
        blueprint.buildings.push_back(copy);
    }
    return blueprint;
}

bool can_rotate(const Blueprint& blueprint) {
    return std::all_of(blueprint.buildings.begin(), blueprint.buildings.end(), [](const BlueprintBuilding& b) {
        const GridSize size = footprint_of(b.kind);
        return is_rotatable(b.kind) || size.w == size.h;
    });
}

Blueprint rotated_cw(const Blueprint& blueprint) {
    assert(can_rotate(blueprint));
    // Cell (x, y) goes to (h - 1 - y, x).
    const GridSize old = blueprint.size;
    Blueprint turned;
    turned.name = blueprint.name;
    turned.size = {old.h, old.w};
    turned.hull.resize(blueprint.hull.size());
    for (int y = 0; y < old.h; ++y)
        for (int x = 0; x < old.w; ++x)
            turned.hull[static_cast<std::size_t>(x * turned.size.w + (old.h - 1 - y))] = blueprint.hull_at({x, y});
    for (BlueprintBuilding b : blueprint.buildings) {
        // Its footprint's bottom-left cell becomes the new top-left one.
        const GridSize size = footprint_of(b.kind, b.facing);
        b.offset = {old.h - b.offset.y - size.h, b.offset.x};
        if (is_rotatable(b.kind))
            b.facing = rotated_cw(b.facing);
        turned.buildings.push_back(b);
    }
    return turned;
}

PastePlan plan_paste(const Ship& ship, const Blueprint& blueprint, GridPos origin) {
    PastePlan plan;
    // Whether each piece fits is checked with an inventory that never runs
    // out; what it costs is tallied separately, in placing order, so a
    // shortfall is reported in full rather than as the first piece missing.
    PlannedChanges planned;
    for (int i = 0; i < kItemKindCount; ++i)
        planned.inventory.add(static_cast<ItemKind>(i), 1 << 24);
    Inventory left = ship.inventory();
    Inventory short_of;

    const auto add = [&](PasteEntry entry) {
        if (!entry.satisfied && entry.result == BuildResult::Ok) {
            if (left.take(entry.kind)) {
                // Upgrading plain hull gives the plain tile back.
                if (entry.kind == ItemKind::ReinforcedHull && ship.hull_at(entry.at) == ItemKind::Hull)
                    left.add(ItemKind::Hull);
            } else {
                short_of.add(entry.kind);
                entry.result = BuildResult::NoneInInventory;
            }
        } else if (!entry.satisfied) {
            ++plan.blocked;
        }
        plan.entries.push_back(entry);
    };

    // Reinforced hull first: upgrades give back plain hull for the rest.
    for (const ItemKind kind : {ItemKind::ReinforcedHull, ItemKind::Hull})
        for (int y = 0; y < blueprint.size.h; ++y)
            for (int x = 0; x < blueprint.size.w; ++x) {
                if (blueprint.hull_at({x, y}) != kind)
                    continue;
                PasteEntry entry;
                entry.at = {origin.x + x, origin.y + y};
                entry.kind = kind;
                const std::optional<ItemKind> there = ship.hull_at(entry.at);
                entry.satisfied = there == kind || there == ItemKind::ReinforcedHull;
                if (!entry.satisfied) {
                    entry.result = ship.can_place(kind, entry.at, Direction::North, planned);
                    if (entry.result == BuildResult::Ok)
                        planned.hull[PlannedChanges::cell_key(entry.at)] = kind;
                }
                add(entry);
            }

    for (std::size_t i = 0; i < blueprint.buildings.size(); ++i) {
        const BlueprintBuilding& b = blueprint.buildings[i];
        PasteEntry entry;
        entry.at = {origin.x + b.offset.x, origin.y + b.offset.y};
        entry.kind = b.kind;
        entry.building = i;
        entry.facing = b.facing;
        const Building* there = ship.building_anchored_at(entry.at);
        entry.satisfied = there && there->item_kind() == b.kind && there->facing() == b.facing;
        if (!entry.satisfied) {
            entry.result = ship.can_place(b.kind, entry.at, b.facing, planned);
            if (entry.result == BuildResult::Ok) {
                const GridSize size = footprint_of(b.kind, b.facing);
                for (int y = entry.at.y; y < entry.at.y + size.h; ++y)
                    for (int x = entry.at.x; x < entry.at.x + size.w; ++x)
                        planned.occupied.insert(PlannedChanges::cell_key({x, y}));
            }
        }
        add(entry);
    }
    plan.missing = short_of.stacks();
    return plan;
}

std::string describe(const PastePlan& plan) {
    if (plan.ok())
        return "Ok";
    std::string text;
    if (plan.blocked > 0) {
        const auto first = std::find_if(plan.entries.begin(), plan.entries.end(), [](const PasteEntry& e) {
            return !e.satisfied && e.result != BuildResult::Ok && e.result != BuildResult::NoneInInventory;
        });
        text = describe(first->result);
        if (plan.blocked > 1)
            text += " (" + std::to_string(plan.blocked) + " in the way)";
    }
    if (!plan.missing.empty()) {
        text += text.empty() ? "Missing " : "; missing ";
        for (std::size_t i = 0; i < plan.missing.size(); ++i)
            text += (i > 0 ? ", " : "") + std::to_string(plan.missing[i].count) + " " + item_name(plan.missing[i].kind);
    }
    return text;
}

PasteResult paste(Ship& ship, const Blueprint& blueprint, GridPos origin) {
    PasteResult result{plan_paste(ship, blueprint, origin), false, 0};
    if (!result.plan.ok())
        return result;
    for (const PasteEntry& entry : result.plan.entries) {
        if (entry.satisfied)
            continue;
        const BuildResult placed = ship.place(entry.kind, entry.at, entry.facing);
        assert(placed == BuildResult::Ok && "plan_paste and Ship::place disagree");
        (void)placed;
        if (!entry.building)
            continue;
        const BlueprintBuilding& b = blueprint.buildings[*entry.building];
        if (b.recipe && !ship.set_recipe(entry.at, b.recipe))
            ++result.settings_skipped;
        if (b.target)
            ship.set_collector_target(entry.at, *b.target);
        if (b.filter)
            ship.set_sort_filter(entry.at, *b.filter);
    }
    result.pasted = true;
    return result;
}
