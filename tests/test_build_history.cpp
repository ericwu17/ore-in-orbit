// test_build_history.cpp — headless tests for undoing build actions: a
// placement, a drag of several as one action, a removal (the building and
// its settings back, paid for again), a blueprint paste as one action,
// settings and the bridge's requests, undoing in reverse order, and the
// actions that can no longer be undone (a building destroyed since,
// something in the way, the inventory short): nothing changes, and the
// action is dropped.
#include "sim/blueprint.h"
#include "sim/build_history.h"
#include "sim/building.h"
#include "sim/deliveries.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace {

// Bare hull east of the bridge on the largest layout (as test_blueprint).
const ShipLayout& kLayout = *find_layout("carrier");
constexpr int c = kShipGridSize / 2;
const int x0 = layout_to_grid(kLayout, kLayout.bridge).x + kBridgeSize + 13;

// An empty ship with plenty of everything placeable.
Ship stocked_ship() {
    Ship ship = Ship::from_layout(kLayout);
    for (int i = 0; i < kItemKindCount; ++i)
        if (is_placeable(static_cast<ItemKind>(i)))
            ship.inventory().add(static_cast<ItemKind>(i), 20);
    return ship;
}

// The first recipe `machine` can run that is unlocked at the start.
const Recipe* unlocked_recipe(ItemKind machine, const Recipe* other_than = nullptr) {
    const Progress progress;
    for (const Recipe* r : machine_recipes(machine))
        if (progress.unlocked(*r) && r != other_than)
            return r;
    assert(false && "no such recipe");
    return nullptr;
}

// Places `kind` at `p` as one action of its own.
void place(BuildHistory& history, Ship& ship, ItemKind kind, GridPos p, Direction facing = Direction::North) {
    history.touch(ship, p);
    assert(ship.place(kind, p, facing) == BuildResult::Ok);
    assert(history.commit(ship));
}

int count(const Ship& ship, ItemKind kind) { return ship.inventory().count(kind); }

void test_place() {
    Ship ship = stocked_ship();
    BuildHistory history;
    const int belts = count(ship, ItemKind::TransportBelt);
    place(history, ship, ItemKind::TransportBelt, {x0, c}, Direction::East);
    assert(history.size() == 1 && count(ship, ItemKind::TransportBelt) == belts - 1);

    const UndoOutcome undone = history.undo(ship);
    assert(undone.result == UndoResult::Ok);
    assert(describe(undone) == "Undone: placing 1 building");
    assert(!ship.building_at({x0, c}));
    assert(count(ship, ItemKind::TransportBelt) == belts);  // refunded
    assert(history.size() == 0);
    assert(history.undo(ship).result == UndoResult::NothingToUndo);

    // Touching without changing anything records nothing.
    history.touch(ship, {x0, c});
    assert(history.recording());
    assert(!history.commit(ship) && history.size() == 0 && !history.recording());
    // Nor does touching the bridge (it can't be changed).
    history.touch(ship, layout_to_grid(kLayout, kLayout.bridge));
    assert(!history.recording());
}

void test_drag_is_one_action() {
    Ship ship = stocked_ship();
    BuildHistory history;
    const int belts = count(ship, ItemKind::TransportBelt);
    // A drag east then turning south: the corner belt taken up and put
    // down again facing the new way, all one action.
    for (int i = 0; i < 4; ++i) {
        history.touch(ship, {x0 + i, c});
        assert(ship.place(ItemKind::TransportBelt, {x0 + i, c}, Direction::East) == BuildResult::Ok);
    }
    history.touch(ship, {x0 + 3, c});
    assert(ship.remove_building({x0 + 3, c}) == BuildResult::Ok);
    assert(ship.place(ItemKind::TransportBelt, {x0 + 3, c}, Direction::South) == BuildResult::Ok);
    history.touch(ship, {x0 + 3, c + 1});
    assert(ship.place(ItemKind::TransportBelt, {x0 + 3, c + 1}, Direction::South) == BuildResult::Ok);
    assert(history.commit(ship) && history.size() == 1);

    const UndoOutcome undone = history.undo(ship);
    assert(undone.result == UndoResult::Ok && undone.action.changes.size() == 5);
    assert(describe(undone.action) == "placing 5 buildings");
    for (int i = 0; i < 4; ++i)
        assert(!ship.building_at({x0 + i, c}));
    assert(!ship.building_at({x0 + 3, c + 1}));
    assert(count(ship, ItemKind::TransportBelt) == belts);
}

void test_remove() {
    Ship ship = stocked_ship();
    BuildHistory history;
    // A collector aimed at silica, running only on request, with a catch
    // in its buffer.
    place(history, ship, ItemKind::AsteroidCollector, {x0, c});
    history.touch(ship, {x0, c});
    assert(ship.set_collector_target({x0, c}, ItemKind::Silica));
    const auto* collector = dynamic_cast<const Collector*>(ship.building_anchored_at({x0, c}));
    for (int i = 0; i < 10000 && collector->stored() == 0; ++i)
        ship.tick();
    assert(collector->stored() > 0);
    assert(ship.set_only_when_requested({x0, c}, true));
    assert(history.commit(ship));
    const int collectors = count(ship, ItemKind::AsteroidCollector);
    const int silica = count(ship, ItemKind::Silica);

    // Removed with a right click on its bottom-right cell.
    history.touch(ship, {x0 + 1, c + 1});
    assert(ship.remove_building({x0 + 1, c + 1}) == BuildResult::Ok);
    assert(history.commit(ship) && history.size() == 3);
    assert(count(ship, ItemKind::AsteroidCollector) == collectors + 1);
    const int refunded_silica = count(ship, ItemKind::Silica);
    assert(refunded_silica > silica);

    // Undone: back where it was, with its settings, paid for again; what
    // it held stays in the inventory (it comes back empty).
    const UndoOutcome undone = history.undo(ship);
    assert(undone.result == UndoResult::Ok);
    assert(describe(undone) == "Undone: removing 1 building");
    collector = dynamic_cast<const Collector*>(ship.building_anchored_at({x0, c}));
    assert(collector && collector->get_target() == ItemKind::Silica && collector->get_only_when_requested());
    assert(collector->stored() == 0);
    assert(count(ship, ItemKind::AsteroidCollector) == collectors);
    assert(count(ship, ItemKind::Silica) == refunded_silica);

    // And the settings, and the placement, before it.
    assert(history.undo(ship).result == UndoResult::Ok);
    assert(collector->get_target() != ItemKind::Silica && !collector->get_only_when_requested());
    assert(history.undo(ship).result == UndoResult::Ok);
    assert(!ship.building_at({x0, c}));
    assert(count(ship, ItemKind::AsteroidCollector) == collectors + 1);
}

void test_paste() {
    Ship ship = stocked_ship();
    BuildHistory history;
    // A row of three belts and a sorter with a filter, copied.
    for (int i = 0; i < 3; ++i)
        place(history, ship, ItemKind::TransportBelt, {x0 + i, c}, Direction::East);
    place(history, ship, ItemKind::FilterSorter, {x0 + 3, c});
    assert(ship.set_filter({x0 + 3, c}, ItemKind::IronOre));
    const Blueprint bp = *copy_region(ship, {x0, c}, {x0 + 3, c});
    history.clear();

    // One belt already where the paste puts its first: it's left alone.
    const GridPos at{x0, c + 2};
    place(history, ship, ItemKind::TransportBelt, at, Direction::East);
    const Inventory before = ship.inventory();
    for (const BlueprintBuilding& b : bp.buildings)
        history.touch(ship, {at.x + b.offset.x, at.y + b.offset.y});
    assert(paste(ship, bp, at).pasted);
    assert(history.commit(ship) && history.size() == 2);

    const UndoOutcome undone = history.undo(ship);
    assert(undone.result == UndoResult::Ok && describe(undone.action) == "placing 3 buildings");
    assert(ship.building_anchored_at(at) && !ship.building_at({at.x + 1, at.y}));
    assert(!ship.building_at({at.x + 3, at.y}));
    for (int i = 0; i < kItemKindCount; ++i)
        assert(ship.inventory().count(static_cast<ItemKind>(i)) == before.count(static_cast<ItemKind>(i)));
    // The copy it came from is untouched.
    assert(ship.building_anchored_at({x0 + 3, c}));
}

void test_settings() {
    Ship ship = stocked_ship();
    BuildHistory history;
    place(history, ship, ItemKind::SolarFurnace, {x0, c});
    place(history, ship, ItemKind::FilterSorter, {x0 + 3, c});
    history.clear();

    // A recipe picked, then a filter, then running only on request.
    const Recipe* first = unlocked_recipe(ItemKind::SolarFurnace);
    history.touch(ship, {x0 + 1, c + 1});
    assert(ship.set_recipe({x0 + 1, c + 1}, first));
    assert(history.commit(ship));
    history.touch(ship, {x0 + 3, c});
    assert(ship.set_filter({x0 + 3, c}, ItemKind::CopperOre));
    assert(history.commit(ship));
    history.touch(ship, {x0, c});
    assert(ship.set_only_when_requested({x0, c}, true));
    assert(history.commit(ship));
    // The bridge asks for iron plates, then for more.
    Bridge& bridge = ship.get_bridge();
    history.touch_requests(bridge);
    bridge.set_request(ItemKind::IronPlate, 100);
    bridge.set_request(ItemKind::IronPlate, 150);
    assert(history.commit(ship) && history.size() == 4);

    // Setting the same again changes nothing: not an action.
    history.touch(ship, {x0 + 3, c});
    assert(ship.set_filter({x0 + 3, c}, ItemKind::CopperOre));
    assert(!history.commit(ship));

    const auto* furnace = dynamic_cast<const Machine*>(ship.building_anchored_at({x0, c}));
    const auto* sorter = dynamic_cast<const Sorter*>(ship.building_anchored_at({x0 + 3, c}));
    UndoOutcome undone = history.undo(ship);
    assert(undone.result == UndoResult::Ok && describe(undone) == "Undone: the bridge's requests");
    assert(bridge.get_requests().empty());
    undone = history.undo(ship);
    assert(undone.result == UndoResult::Ok && describe(undone.action) == "the settings of 1 building");
    assert(furnace->get_recipe() == first && !furnace->get_only_when_requested());
    assert(history.undo(ship).result == UndoResult::Ok);
    assert(!sorter->get_filter());
    assert(history.undo(ship).result == UndoResult::Ok);
    assert(!furnace->get_recipe());
    // The buildings themselves stayed put all along.
    assert(ship.building_anchored_at({x0, c}) == furnace && ship.building_anchored_at({x0 + 3, c}) == sorter);
    assert(history.undo(ship).result == UndoResult::NothingToUndo);

    // A removed machine comes back with its recipe.
    history.touch(ship, {x0, c});
    assert(ship.set_recipe({x0, c}, first));
    assert(history.commit(ship));
    history.touch(ship, {x0, c});
    assert(ship.remove_building({x0, c}) == BuildResult::Ok);
    assert(history.commit(ship) && history.size() == 2);
    assert(history.undo(ship).result == UndoResult::Ok);
    furnace = dynamic_cast<const Machine*>(ship.building_anchored_at({x0, c}));
    assert(furnace && furnace->get_recipe() == first);
    // Both in one action: back to before either (no recipe).
    history.clear();
    history.touch(ship, {x0, c});
    assert(ship.set_recipe({x0, c}, nullptr));
    assert(ship.remove_building({x0, c}) == BuildResult::Ok);
    assert(history.commit(ship) && history.undo(ship).result == UndoResult::Ok);
    furnace = dynamic_cast<const Machine*>(ship.building_anchored_at({x0, c}));
    assert(furnace && furnace->get_recipe() == first);
}

void test_cannot_undo() {
    Ship ship = stocked_ship();
    BuildHistory history;

    // Destroyed since (combat): the placement can't be undone; dropped,
    // so the next undo reaches the one before.
    place(history, ship, ItemKind::TransportBelt, {x0, c}, Direction::East);
    place(history, ship, ItemKind::FabricatorMk1, {x0 + 2, c});
    const int fabricators = count(ship, ItemKind::FabricatorMk1);
    ship.destroy_building({x0 + 2, c});
    UndoOutcome outcome = history.undo(ship);
    assert(outcome.result == UndoResult::Changed);
    assert(describe(outcome).find("Can't undo placing 1 building") == 0);
    assert(count(ship, ItemKind::FabricatorMk1) == fabricators);  // nothing refunded for it
    assert(history.size() == 1);
    assert(history.undo(ship).result == UndoResult::Ok && !ship.building_at({x0, c}));

    // The inventory no longer has the building a removal refunded.
    place(history, ship, ItemKind::Refinery, {x0, c});
    history.touch(ship, {x0, c});
    assert(ship.remove_building({x0, c}) == BuildResult::Ok);
    assert(history.commit(ship));
    assert(ship.inventory().take(ItemKind::Refinery, count(ship, ItemKind::Refinery)));
    outcome = history.undo(ship);
    assert(outcome.result == UndoResult::NoneInInventory);
    assert(outcome.missing.size() == 1 && outcome.missing[0].kind == ItemKind::Refinery);
    assert(describe(outcome).find("missing 1 Refinery") != std::string::npos);
    assert(!ship.building_at({x0, c}));
    // Dropped too: on to the placement before it, now nothing to take away.
    assert(history.size() == 1);
    assert(history.undo(ship).result == UndoResult::Changed);
    ship.inventory().add(ItemKind::Refinery, 5);

    // Something in the way of what would go back (put there without the
    // history knowing).
    place(history, ship, ItemKind::Refinery, {x0, c});
    history.touch(ship, {x0, c});
    assert(ship.remove_building({x0, c}) == BuildResult::Ok);
    assert(history.commit(ship));
    assert(ship.place(ItemKind::TransportBelt, {x0 + 2, c + 2}) == BuildResult::Ok);
    const int refineries = count(ship, ItemKind::Refinery);
    outcome = history.undo(ship);
    assert(outcome.result == UndoResult::Blocked);
    assert(!ship.building_anchored_at({x0, c}) && count(ship, ItemKind::Refinery) == refineries);

    // But what the action itself built doesn't count as in the way: a belt
    // put down where a removed refinery stood, in one action, swaps back.
    history.clear();
    assert(ship.remove_building({x0 + 2, c + 2}) == BuildResult::Ok);
    assert(ship.place(ItemKind::Refinery, {x0, c}) == BuildResult::Ok);
    history.touch(ship, {x0, c});
    assert(ship.remove_building({x0, c}) == BuildResult::Ok);
    history.touch(ship, {x0 + 1, c + 1});
    assert(ship.place(ItemKind::TransportBelt, {x0 + 1, c + 1}) == BuildResult::Ok);
    assert(history.commit(ship));
    outcome = history.undo(ship);
    assert(outcome.result == UndoResult::Ok);
    assert(ship.building_anchored_at({x0, c})->item_kind() == ItemKind::Refinery);
}

void test_limit() {
    Ship ship = stocked_ship();
    ship.inventory().add(ItemKind::TransportBelt, 1000);
    BuildHistory history;
    for (std::size_t i = 0; i < BuildHistory::kMaxActions + 5; ++i) {
        const GridPos p{x0 + static_cast<int>(i % 20), c + static_cast<int>(i / 20)};
        history.touch(ship, p);
        assert(ship.place(ItemKind::TransportBelt, p, Direction::East) == BuildResult::Ok);
        assert(history.commit(ship));
    }
    assert(history.size() == BuildHistory::kMaxActions);
    // An open action is committed before undoing, and is what's undone.
    history.touch(ship, {x0 + 10, c + 10});
    assert(ship.place(ItemKind::TransportBelt, {x0 + 10, c + 10}) == BuildResult::Ok);
    assert(history.undo(ship).result == UndoResult::Ok && !ship.building_at({x0 + 10, c + 10}));
    assert(history.size() == BuildHistory::kMaxActions - 1);
}

}  // namespace

int main() {
    test_place();
    test_drag_is_one_action();
    test_remove();
    test_paste();
    test_settings();
    test_cannot_undo();
    test_limit();
    std::puts("test_build_history: all passed");
    return 0;
}
