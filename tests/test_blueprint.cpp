// test_blueprint.cpp — headless tests for blueprints: copying part of a ship
// (its buildings and their settings, never their items, nor the layout's
// own parts), turning a copy, pasting it all or nothing onto the hull there
// is, blueprints as strings (and refusing damaged ones with a reason, and
// reading old ones with hull), and the blueprint library folder.
#include "persistence/blueprint_file.h"
#include "sim/blueprint.h"
#include "sim/building.h"
#include "sim/milestones.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"

#include "nlohmann/json.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

namespace {

// The tests build on the largest layout, for room: its hull's widest part
// spans the grid's rows y0..y1 and columns from its origin to x_end.
const ShipLayout& kLayout = *find_layout("carrier");
const HullRect& kBody = kLayout.hull.at(2);
const int y0 = layout_to_grid(kLayout, kBody.at).y;
const int x_end = layout_to_grid(kLayout, kBody.at).x + kBody.size.w - 1;
// A row through the bridge, and a column east of it: bare hull to build on.
constexpr int c = kShipGridSize / 2;
const int x0 = layout_to_grid(kLayout, kLayout.bridge).x + kBridgeSize + 13;

// An empty ship in kLayout.
Ship empty_ship() {
    Ship ship = Ship::from_layout(kLayout);
    assert(y0 <= c && c + 10 < y0 + kBody.size.h && x0 + 20 <= x_end);
    return ship;
}

// The same, with plenty of everything placeable.
Ship stocked_ship() {
    Ship ship = empty_ship();
    for (int i = 0; i < kItemKindCount; ++i)
        if (is_placeable(static_cast<ItemKind>(i)))
            ship.inventory().add(static_cast<ItemKind>(i), 20);
    return ship;
}

// The first recipe `machine` can run that is (or isn't) unlocked at the
// start of a game.
const Recipe* machine_recipe(ItemKind machine, bool unlocked) {
    const Progress progress;
    for (const Recipe* r : machine_recipes(machine))
        if (progress.unlocked(*r) == unlocked)
            return r;
    assert(false && "no such recipe");
    return nullptr;
}

const Recipe* const kFurnaceRecipe = machine_recipe(ItemKind::SolarFurnace, true);

// A 6x4 patch east of the bridge, laid out (. bare hull):
//
//   A A v R R R      A  asteroid collector (aimed at silica), 2x2
//   A A v R R R      v  belts facing south (one carrying an item)
//   . F F R R R      F  solar furnace (with a recipe), 2x2
//   S F F . . .      R  refinery, 3x3
//                    S  filter sorter (filtering iron ore)
void build_patch(Ship& ship) {
    assert(ship.place(ItemKind::AsteroidCollector, {x0, c}) == BuildResult::Ok);
    assert(ship.set_collector_target({x0, c}, ItemKind::Silica));
    assert(ship.place(ItemKind::TransportBelt, {x0 + 2, c}, Direction::South) == BuildResult::Ok);
    assert(ship.place(ItemKind::TransportBelt, {x0 + 2, c + 1}, Direction::South) == BuildResult::Ok);
    assert(ship.insert_item({x0 + 2, c}, {x0 + 2, c - 1}, ItemKind::IronOre));
    assert(ship.place(ItemKind::SolarFurnace, {x0 + 1, c + 2}) == BuildResult::Ok);
    assert(ship.set_recipe({x0 + 1, c + 2}, kFurnaceRecipe));
    assert(ship.place(ItemKind::Refinery, {x0 + 3, c}) == BuildResult::Ok);
    assert(ship.place(ItemKind::FilterSorter, {x0, c + 3}) == BuildResult::Ok);
    assert(ship.set_sort_filter({x0, c + 3}, ItemKind::IronOre));
}

const BlueprintBuilding* find(const Blueprint& blueprint, ItemKind kind) {
    for (const BlueprintBuilding& b : blueprint.buildings)
        if (b.kind == kind)
            return &b;
    return nullptr;
}

void test_copy() {
    Ship ship = stocked_ship();
    build_patch(ship);

    // The whole patch: every building, with settings, in placing order.
    const std::optional<Blueprint> bp = copy_region(ship, {x0 + 5, c + 3}, {x0, c});
    assert(bp && bp->name.empty());
    assert(bp->size == (GridSize{6, 4}));
    assert(bp->buildings.size() == 6);
    assert(bp->buildings[0].kind == ItemKind::AsteroidCollector && bp->buildings[5].kind == ItemKind::FilterSorter);
    const BlueprintBuilding* collector = find(*bp, ItemKind::AsteroidCollector);
    assert(collector->offset == (GridPos{0, 0}) && collector->target == ItemKind::Silica && !collector->recipe);
    const BlueprintBuilding* belt = find(*bp, ItemKind::TransportBelt);
    assert(belt->offset == (GridPos{2, 0}) && belt->facing == Direction::South);
    assert(find(*bp, ItemKind::SolarFurnace)->recipe == kFurnaceRecipe);
    assert(find(*bp, ItemKind::Refinery)->offset == (GridPos{3, 0}) && !find(*bp, ItemKind::Refinery)->recipe);
    const BlueprintBuilding* sorter = find(*bp, ItemKind::FilterSorter);
    assert(sorter->offset == (GridPos{0, 3}) && sorter->filter == ItemKind::IronOre);

    // A box over one cell of the collector takes the whole collector.
    const std::optional<Blueprint> part = copy_region(ship, {x0 + 1, c + 1}, {x0 + 1, c + 1});
    assert(part && part->size == (GridSize{2, 2}) && part->buildings.size() == 1);
    assert(part->buildings[0].kind == ItemKind::AsteroidCollector && part->buildings[0].offset == (GridPos{0, 0}));

    // A box partly over the refinery and mostly bare is trimmed to it.
    const std::optional<Blueprint> trimmed = copy_region(ship, {x0 + 4, c + 2}, {x0 + 10, c + 8});
    assert(trimmed && trimmed->size == (GridSize{3, 3}) && trimmed->buildings.size() == 1);
    assert(trimmed->buildings[0].kind == ItemKind::Refinery);

    // Never the layout's own parts: the bridge and the engines.
    const GridPos bridge = layout_to_grid(kLayout, kLayout.bridge);
    assert(!copy_region(ship, bridge, {bridge.x + 2, bridge.y + 2}));
    const GridPos engine = layout_to_grid(kLayout, kLayout.engines.front());
    assert(!copy_region(ship, engine, {engine.x + 1, engine.y + 2}));

    // Nothing there: nothing to copy.
    assert(!copy_region(ship, {x0 + 10, 5}, {x0 + 20, 10}));
}

void test_paste() {
    Ship ship = stocked_ship();
    build_patch(ship);
    const Blueprint bp = *copy_region(ship, {x0, c}, {x0 + 5, c + 3});

    // Onto bare hull: the buildings with their settings; no items come with
    // them.
    const GridPos at{x0 + 10, c};
    const Inventory before = ship.inventory();
    PasteResult result = paste(ship, bp, at);
    assert(result.pasted && result.plan.ok() && result.settings_skipped == 0);
    for (const ItemKind kind : {ItemKind::AsteroidCollector, ItemKind::SolarFurnace, ItemKind::Refinery,
                                ItemKind::FilterSorter})
        assert(ship.inventory().count(kind) == before.count(kind) - 1);
    assert(ship.inventory().count(ItemKind::TransportBelt) == before.count(ItemKind::TransportBelt) - 2);
    assert(ship.building_anchored_at({at.x + 3, at.y})->item_kind() == ItemKind::Refinery);
    const auto* collector = dynamic_cast<const Collector*>(ship.building_anchored_at(at));
    assert(collector && collector->get_target() == ItemKind::Silica && collector->stored() == 0);
    const auto* belt = dynamic_cast<const Conveyor*>(ship.building_anchored_at({at.x + 2, at.y}));
    assert(belt && belt->facing() == Direction::South && belt->get_items().empty());
    const auto* furnace = dynamic_cast<const Machine*>(ship.building_anchored_at({at.x + 1, at.y + 2}));
    assert(furnace && furnace->get_recipe() == kFurnaceRecipe);
    const auto* sorter = dynamic_cast<const Sorter*>(ship.building_anchored_at({at.x, at.y + 3}));
    assert(sorter && sorter->get_filter() == ItemKind::IronOre);

    // Again on top of itself: all of it is already there, and costs nothing.
    const Inventory after = ship.inventory();
    result = paste(ship, bp, at);
    assert(result.pasted);
    for (const PasteEntry& e : result.plan.entries)
        assert(e.satisfied);
    for (int i = 0; i < kItemKindCount; ++i)
        assert(ship.inventory().count(static_cast<ItemKind>(i)) == after.count(static_cast<ItemKind>(i)));

    // Shifted over it: buildings in the way, so nothing at all is placed.
    const GridPos shifted{at.x + 2, at.y};
    result = paste(ship, bp, shifted);
    assert(!result.pasted && result.plan.blocked > 0 && result.plan.missing.empty());
    for (int i = 0; i < kItemKindCount; ++i)
        assert(ship.inventory().count(static_cast<ItemKind>(i)) == after.count(static_cast<ItemKind>(i)));
    assert(describe(result.plan).find(describe(BuildResult::Occupied)) == 0);

    // Hanging off the hull's edge, or the grid's: blocked too.
    assert(plan_paste(ship, bp, {x_end - 5, c}).ok());
    const PastePlan off_hull = plan_paste(ship, bp, {x_end - 4, c});
    assert(!off_hull.ok() && describe(off_hull).find(describe(BuildResult::NoHull)) == 0);
    assert(!plan_paste(ship, bp, {kShipGridSize - 3, c}).ok());

    // Short of items: what's missing, in full; nothing placed.
    Ship poor = empty_ship();
    result = paste(poor, bp, {x0, c});
    assert(!result.pasted && result.plan.blocked == 0);
    const std::string why = describe(result.plan);
    assert(why.find("Missing ") == 0 && why.find("2 Transport belt") != std::string::npos &&
           why.find("1 Refinery") != std::string::npos);
    assert(!poor.building_at({x0, c}));
}

void test_paste_settings() {
    // A recipe not unlocked yet: the machine goes down without it.
    Ship ship = stocked_ship();
    Blueprint fab;
    fab.size = {3, 3};
    BlueprintBuilding machine;
    machine.kind = ItemKind::FabricatorMk1;
    machine.recipe = machine_recipe(ItemKind::FabricatorMk1, false);
    fab.buildings.push_back(machine);
    const PasteResult locked = paste(ship, fab, {x0 + 5, c});
    assert(locked.pasted && locked.settings_skipped == 1);
    const auto* placed = dynamic_cast<const Machine*>(ship.building_anchored_at({x0 + 5, c}));
    assert(placed && !placed->get_recipe());
}

void test_rotate() {
    Ship ship = stocked_ship();
    build_patch(ship);
    const Blueprint bp = *copy_region(ship, {x0, c}, {x0 + 5, c + 3});

    // A quarter turn: (x, y) to (h - 1 - y, x), footprints and belts turned.
    const Blueprint turned = rotated_cw(bp);
    assert(turned.size == (GridSize{4, 6}));
    const BlueprintBuilding* belt = find(turned, ItemKind::TransportBelt);
    assert(belt->offset == (GridPos{3, 2}) && belt->facing == Direction::West);
    assert(find(turned, ItemKind::AsteroidCollector)->offset == (GridPos{2, 0}));
    assert(find(turned, ItemKind::Refinery)->offset == (GridPos{1, 3}));
    assert(find(turned, ItemKind::FilterSorter)->offset == (GridPos{0, 0}));
    assert(find(turned, ItemKind::FilterSorter)->facing == Direction::North);

    // Four turns: back where it started.
    assert(rotated_cw(rotated_cw(rotated_cw(turned))) == bp);

    // A turned copy pastes cleanly (its pieces still fit together).
    const PasteResult result = paste(ship, turned, {x0 + 10, c});
    assert(result.pasted);
    const auto* pasted_belt = dynamic_cast<const Conveyor*>(ship.building_anchored_at({x0 + 13, c + 2}));
    assert(pasted_belt && pasted_belt->facing() == Direction::West);
}

// The blueprint string `text` with `change` made to its JSON.
std::string changed(const std::string& text, const std::function<void(Json&)>& change) {
    Json j = Json::parse(text);
    change(j);
    return j.dump();
}

void refused(const std::string& text) {
    std::string error;
    assert(!blueprint_from_string(text, error));
    assert(!error.empty());
}

void test_strings() {
    // The format, pinned.
    Blueprint tiny;
    tiny.name = "Tiny";
    tiny.size = {2, 1};
    BlueprintBuilding belt;
    belt.facing = Direction::East;
    tiny.buildings.push_back(belt);
    const std::string text = blueprint_to_string(tiny);
    assert(text == R"({"format":"ore-in-orbit-blueprint","version":2,"name":"Tiny","width":2,"height":1,)"
                   R"("buildings":[{"kind":"TransportBelt","x":0,"y":0,"facing":"East"}]})");
    std::string error;
    assert(blueprint_from_string("  \n" + text + "\n", error) == tiny);

    // Version 1 had hull rows (from when the player laid hull): it still
    // reads, without them.
    const std::string v1 =
        R"({"format":"ore-in-orbit-blueprint","version":1,"name":"Tiny","width":2,"height":1,"hull":["HR"],)"
        R"("buildings":[{"kind":"TransportBelt","x":0,"y":0,"facing":"East"}]})";
    assert(blueprint_from_string(v1, error) == tiny);

    // Everything survives the round trip, settings included.
    Ship ship = stocked_ship();
    build_patch(ship);
    Blueprint bp = *copy_region(ship, {x0, c}, {x0 + 5, c + 3});
    bp.name = "Iron line \"v2\"";
    const std::string full = blueprint_to_string(bp);
    assert(full.find('\n') == std::string::npos);
    assert(blueprint_from_string(full, error) == bp);

    // Damaged or foreign strings are refused, with a reason.
    refused("");
    refused("not a blueprint");
    refused("[1, 2]");
    refused(changed(full, [](Json& j) { j["format"] = "ore-in-orbit-save"; }));
    refused(changed(full, [](Json& j) { j["version"] = kBlueprintVersion + 1; }));
    refused(changed(full, [](Json& j) { j.erase("name"); }));
    refused(changed(full, [](Json& j) { j["width"] = 0; }));
    refused(changed(full, [](Json& j) { j["height"] = kShipGridSize + 1; }));
    refused(changed(full, [](Json& j) { j["buildings"][0]["kind"] = "Banana"; }));
    refused(changed(full, [](Json& j) { j["buildings"][0]["kind"] = "Hull"; }));         // not an item any more
    refused(changed(full, [](Json& j) { j["buildings"][0]["kind"] = "IonThruster"; }));  // the layout's alone
    refused(changed(full, [](Json& j) { j["buildings"][0]["kind"] = "IronPlate"; }));
    refused(changed(full, [](Json& j) { j["buildings"][0]["x"] = -1; }));
    refused(changed(full, [](Json& j) { j["buildings"][0]["x"] = 5; }));  // a 2x2 sticking out
    refused(changed(full, [](Json& j) { j["buildings"][1]["y"] = 2; }));  // onto the furnace
    refused(changed(full, [](Json& j) { j["buildings"][1]["facing"] = "Up"; }));
    refused(changed(full, [](Json& j) { j["buildings"][0]["facing"] = "East"; }));  // a collector doesn't turn
    refused(changed(full, [](Json& j) { j["buildings"][0]["target"] = "IridiumOre"; }));
    refused(changed(full, [](Json& j) { j["buildings"][1]["target"] = "IronOre"; }));
    refused(changed(full, [](Json& j) { j["buildings"][1]["filter"] = "IronOre"; }));
    refused(changed(full, [](Json& j) { j["buildings"][1]["recipe"] = "Gear"; }));
    refused(changed(full, [](Json& j) {
        for (Json& b : j["buildings"])
            if (b["kind"] == "SolarFurnace")
                b["recipe"] = "Gear";  // an assembly recipe, in a furnace
    }));
    refused(changed(full, [](Json& j) {
        for (Json& b : j["buildings"])
            if (b["kind"] == "SolarFurnace")
                b["recipe"] = "Moonshine";
    }));
    // A sorter's filter may be cleared.
    const std::string cleared = changed(full, [](Json& j) {
        for (Json& b : j["buildings"])
            if (b["kind"] == "FilterSorter")
                b["filter"] = nullptr;
    });
    const std::optional<Blueprint> unfiltered = blueprint_from_string(cleared, error);
    assert(unfiltered && !find(*unfiltered, ItemKind::FilterSorter)->filter);
}

void test_library(const fs::path& dir) {
    assert(list_blueprints(dir).empty());
    std::string error;
    Blueprint a;
    a.name = "smelter";
    a.size = {1, 1};
    a.buildings.push_back({});
    Blueprint b = a;
    b.name = "Belts";

    const std::optional<fs::path> first = add_blueprint(dir, a, error);
    const std::optional<fs::path> second = add_blueprint(dir, b, error);
    assert(first && second && *first != *second);

    // By name, ignoring case.
    std::vector<BlueprintSlot> slots = list_blueprints(dir);
    assert(slots.size() == 2);
    assert(slots[0].path == *second && slots[0].blueprint == b);
    assert(slots[1].path == *first && slots[1].blueprint == a);

    // A library file is the export string, so either can be read as the
    // other.
    std::ifstream in(*first);
    std::string line;
    std::getline(in, line);
    assert(line == blueprint_to_string(a));

    assert(rename_blueprint(*first, "Assembly", error));
    slots = list_blueprints(dir);
    assert(slots[0].path == *first && slots[0].blueprint->name == "Assembly");

    // A damaged file is listed last, with the reason; leftovers from an
    // interrupted write and other files aren't listed at all.
    std::ofstream(dir / "broken.json") << "{}";
    std::ofstream(dir / "blueprint-9.json.tmp") << "half";
    std::ofstream(dir / "notes.txt") << "hello";
    slots = list_blueprints(dir);
    assert(slots.size() == 3);
    assert(slots[2].path.filename() == "broken.json" && !slots[2].blueprint && !slots[2].error.empty());
    error.clear();
    assert(!rename_blueprint(dir / "broken.json", "Fixed", error) && !error.empty());

    assert(delete_blueprint(*second, error));
    assert(delete_blueprint(dir / "broken.json", error));
    assert(list_blueprints(dir).size() == 1);
    assert(!delete_blueprint(*second, error) && !error.empty());
}

}  // namespace

int main() {
    test_copy();
    test_paste();
    test_paste_settings();
    test_rotate();
    test_strings();
    const fs::path dir = fs::temp_directory_path() / "ore-in-orbit-test-blueprints";
    fs::remove_all(dir);
    test_library(dir / "blueprints");
    fs::remove_all(dir);
    std::puts("test_blueprint: all passed");
    return 0;
}
