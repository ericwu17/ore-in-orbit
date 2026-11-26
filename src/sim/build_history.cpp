// build_history.cpp — undoing build actions (see build_history.h).
#include "sim/build_history.h"

#include "sim/building.h"

#include <algorithm>
#include <cassert>

namespace {

// Whether the change takes a building away or puts one down (rather than
// only changing the settings of one that stays).
bool structural(const BuildChange& c) {
    return !c.before || !c.after || c.before->kind != c.after->kind || c.before->facing != c.after->facing;
}

std::string count_of(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

// Sets the building at `at` to `want`'s settings, changing only those that
// differ (each change refunds what it held, as in the inspector). Returns
// how many recipes couldn't be set back (no longer allowed): those are
// left as they are.
int restore_settings(Ship& ship, GridPos at, const BlueprintBuilding& want) {
    const Building* b = ship.building_anchored_at(at);
    assert(b);
    const BlueprintBuilding now = blueprint_building(*b, at);
    int skipped = 0;
    if (want.recipe != now.recipe && !ship.set_recipe(at, want.recipe))
        ++skipped;
    if (want.target && want.target != now.target)
        ship.set_collector_target(at, *want.target);
    if (want.filter != now.filter)
        ship.set_filter(at, want.filter);
    if (want.only_when_requested != now.only_when_requested)
        ship.set_only_when_requested(at, want.only_when_requested);
    return skipped;
}

bool same_requests(const std::vector<ItemStack>& a, const std::vector<ItemStack>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](const ItemStack& x, const ItemStack& y) { return x.kind == y.kind && x.count == y.count; });
}

void restore_requests(Bridge& bridge, const std::vector<ItemStack>& requests) {
    const std::vector<ItemStack> now = bridge.get_requests();
    for (const ItemStack& r : now)
        bridge.remove_request(r.kind);
    for (const ItemStack& r : requests)
        bridge.set_request(r.kind, r.count);
}

}  // namespace

std::string describe(const BuildAction& action) {
    int placed = 0, removed = 0, rebuilt = 0, settings = 0;
    for (const BuildChange& c : action.changes) {
        if (!c.before)
            ++placed;
        else if (!c.after)
            ++removed;
        else if (structural(c))
            ++rebuilt;
        else
            ++settings;
    }
    std::vector<std::string> parts;
    if (placed > 0)
        parts.push_back("placing " + count_of(placed, "building", "buildings"));
    if (removed > 0)
        parts.push_back("removing " + count_of(removed, "building", "buildings"));
    if (rebuilt > 0)
        parts.push_back("rebuilding " + count_of(rebuilt, "building", "buildings"));
    if (settings > 0)
        parts.push_back("the settings of " + count_of(settings, "building", "buildings"));
    if (action.requests_changed)
        parts.push_back("the bridge's requests");
    if (parts.empty())
        return "nothing";
    std::string text;
    for (std::size_t i = 0; i < parts.size(); ++i)
        text += (i == 0 ? "" : i + 1 == parts.size() ? " and " : ", ") + parts[i];
    return text;
}

std::string describe(const UndoOutcome& outcome) {
    const std::string what = describe(outcome.action);
    switch (outcome.result) {
    case UndoResult::Ok:
        return "Undone: " + what;
    case UndoResult::NothingToUndo:
        return "Nothing to undo";
    case UndoResult::Changed:
        return "Can't undo " + what + ": the ship has changed there since. Skipped it";
    case UndoResult::Blocked:
        return "Can't undo " + what + ": something is built in the way. Skipped it";
    case UndoResult::NoneInInventory: {
        std::string text = "Can't undo " + what + ": missing ";
        for (std::size_t i = 0; i < outcome.missing.size(); ++i)
            text += (i > 0 ? ", " : "") + std::to_string(outcome.missing[i].count) + " " +
                    item_name(outcome.missing[i].kind);
        return text + ". Skipped it";
    }
    }
    return what;
}

std::optional<BlueprintBuilding> building_record(const Ship& ship, GridPos at) {
    const Building* b = ship.in_bounds(at) ? ship.building_anchored_at(at) : nullptr;
    if (!b || !b->removable() || !b->item_kind())
        return std::nullopt;
    return blueprint_building(*b, at);
}

void BuildHistory::touch(const Ship& ship, GridPos cell) {
    if (!ship.in_bounds(cell))
        return;
    const GridPos at = ship.anchor_of(cell).value_or(cell);
    if (const Building* b = ship.building_anchored_at(at); b && !b->removable())
        return;  // the bridge or an engine: nothing the player can change there
    is_open = true;
    const auto same = [&](const BuildChange& c) { return c.at == at; };
    if (std::none_of(open.changes.begin(), open.changes.end(), same))
        open.changes.push_back({at, building_record(ship, at), std::nullopt});
}

void BuildHistory::touch_requests(const Bridge& bridge) {
    is_open = true;
    if (!open.requests_changed) {
        open.requests_changed = true;
        open.requests_before = bridge.get_requests();
    }
}

bool BuildHistory::commit(const Ship& ship) {
    if (!is_open)
        return false;
    BuildAction action;
    for (BuildChange& c : open.changes) {
        c.after = building_record(ship, c.at);
        if (c.before != c.after)
            action.changes.push_back(std::move(c));
    }
    if (open.requests_changed && !same_requests(open.requests_before, ship.get_bridge().get_requests())) {
        action.requests_changed = true;
        action.requests_before = std::move(open.requests_before);
        action.requests_after = ship.get_bridge().get_requests();
    }
    open = {};
    is_open = false;
    if (action.changes.empty() && !action.requests_changed)
        return false;
    done.push_back(std::move(action));
    if (done.size() > kMaxActions)
        done.erase(done.begin());
    return true;
}

UndoOutcome BuildHistory::undo(Ship& ship) {
    commit(ship);
    UndoOutcome outcome;
    if (done.empty())
        return outcome;
    outcome.action = std::move(done.back());
    done.pop_back();
    const BuildAction& action = outcome.action;

    // The ship must still stand as the action left it.
    for (const BuildChange& c : action.changes) {
        if (building_record(ship, c.at) != c.after) {
            outcome.result = UndoResult::Changed;
            return outcome;
        }
    }

    // What goes back must fit, once what the action built is gone...
    std::vector<GridPos> going;
    for (const BuildChange& c : action.changes)
        if (structural(c) && c.after)
            going.push_back(c.at);
    for (const BuildChange& c : action.changes) {
        if (!structural(c) || !c.before)
            continue;
        const GridSize size = footprint_of(c.before->kind, c.before->facing);
        for (int y = c.at.y; y < c.at.y + size.h; ++y) {
            for (int x = c.at.x; x < c.at.x + size.w; ++x) {
                const GridPos cell{x, y};
                if (!ship.in_bounds(cell) || !ship.has_hull(cell)) {
                    outcome.result = UndoResult::Blocked;
                    return outcome;
                }
                const std::optional<GridPos> there = ship.anchor_of(cell);
                if (there && std::find(going.begin(), going.end(), *there) == going.end()) {
                    outcome.result = UndoResult::Blocked;
                    return outcome;
                }
            }
        }
        if (ship.check_ports(c.before->kind, c.at, size) != BuildResult::Ok) {
            outcome.result = UndoResult::Blocked;
            return outcome;
        }
    }
    // ...and be paid for, with what taking that away refunds.
    Inventory left = ship.inventory();
    Inventory short_of;
    for (const BuildChange& c : action.changes)
        if (structural(c) && c.after)
            left.add(c.after->kind);
    for (const BuildChange& c : action.changes)
        if (structural(c) && c.before && !left.take(c.before->kind))
            short_of.add(c.before->kind);
    if (short_of.total() > 0) {
        outcome.result = UndoResult::NoneInInventory;
        outcome.missing = short_of.stacks();
        return outcome;
    }

    // Take away what it built, then put back what it removed, then the
    // settings.
    for (auto c = action.changes.rbegin(); c != action.changes.rend(); ++c) {
        if (structural(*c) && c->after) {
            const BuildResult removed = ship.remove_building(c->at);
            assert(removed == BuildResult::Ok);
            (void)removed;
        }
    }
    for (const BuildChange& c : action.changes) {
        if (structural(c) && c.before) {
            const BuildResult placed = ship.place(c.before->kind, c.at, c.before->facing);
            assert(placed == BuildResult::Ok && "the undo checks and Ship::place disagree");
            (void)placed;
        }
    }
    for (const BuildChange& c : action.changes)
        if (c.before)
            outcome.settings_skipped += restore_settings(ship, c.at, *c.before);
    if (action.requests_changed)
        restore_requests(ship.get_bridge(), action.requests_before);
    outcome.result = UndoResult::Ok;
    return outcome;
}

void BuildHistory::clear() {
    done.clear();
    open = {};
    is_open = false;
}
