// test_save_file.cpp — headless tests for saving a game as JSON and loading
// it back: every building's state survives the round trip, a loaded game
// carries on exactly as the original would have, and damaged or foreign
// files are refused with a reason (never an assert).
#include "persistence/save_file.h"
#include "sim/building.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/world.h"

#include "nlohmann/json.hpp"

#include <cassert>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>

using Json = nlohmann::ordered_json;

namespace {

// Rows near the middle of the grid, through the bridge; and the first
// column east of the starting hull.
constexpr int c = kShipGridSize / 2;
constexpr int x0 = (kShipGridSize + kStartHullSide) / 2;

void run(World& world, Tick ticks) {
    for (Tick t = 0; t < ticks; ++t)
        world.advance_tick();
}

std::string save_text(const World& world) {
    std::ostringstream out;
    write_save(out, "Test ship", 1'700'000'000, world);
    return out.str();
}

std::optional<LoadedSave> load_text(const std::string& text, std::string& error) {
    std::istringstream in(text);
    return read_save(in, error);
}

const Recipe& recipe_named(const char* name) {
    for (const Recipe& r : all_recipes())
        if (std::string(recipe_name(r)) == name)
            return r;
    assert(false && "no such recipe");
    return all_recipes().front();
}

// A ship with one of everything that holds state, part way through running:
// a collector (aimed at silica) feeding belts into a cargo hold, a solar
// panel, a filter sorter with items queued on the belt into it, two machines
// mid-craft (one on reinforced hull), a west-facing express belt, a junction
// and a router holding items, and the bridge hand-crafting, part way through
// a milestone.
World busy_world() {
    World world;
    world.get_ship().inventory().add({{ItemKind::Hull, 200},
                                      {ItemKind::ReinforcedHull, 9},
                                      {ItemKind::TransportBelt, 10},
                                      {ItemKind::ExpressBelt, 1},
                                      {ItemKind::FilterSorter, 1},
                                      {ItemKind::Junction, 1},
                                      {ItemKind::Router, 1},
                                      {ItemKind::SolarPanel, 1},
                                      {ItemKind::Refinery, 1}});
    // Two milestones done (the refinery's recipes need them), and some way
    // into the third.
    Progress& progress = world.get_ship().get_bridge().get_progress();
    assert(progress.complete_current() && progress.complete_current());
    for (int i = 0; i < 3; ++i)
        assert(!progress.on_delivered(ItemKind::AdvancedCircuit));
    Ship& ship = world.get_ship();
    for (int y = c - 6; y <= c + 6; ++y)
        for (int x = x0; x <= x0 + 9; ++x)
            assert(ship.place(ItemKind::Hull, {x, y}) == BuildResult::Ok);
    for (int y = c + 2; y <= c + 4; ++y)
        for (int x = x0 + 5; x <= x0 + 7; ++x)
            assert(ship.place(ItemKind::ReinforcedHull, {x, y}) == BuildResult::Ok);

    assert(ship.place(ItemKind::AsteroidCollector, {x0, c - 6}) == BuildResult::Ok);
    assert(ship.set_collector_target({x0, c - 6}, ItemKind::Silica));
    for (int x = x0 + 2; x <= x0 + 4; ++x)
        assert(ship.place(ItemKind::TransportBelt, {x, c - 6}, Direction::East) == BuildResult::Ok);
    assert(ship.place(ItemKind::CargoHold, {x0 + 5, c - 6}) == BuildResult::Ok);
    assert(ship.place(ItemKind::SolarPanel, {x0 + 8, c - 6}) == BuildResult::Ok);

    assert(ship.place(ItemKind::TransportBelt, {x0 + 2, c - 2}, Direction::East) == BuildResult::Ok);
    assert(ship.place(ItemKind::FilterSorter, {x0 + 3, c - 2}) == BuildResult::Ok);
    assert(ship.set_sort_filter({x0 + 3, c - 2}, ItemKind::IronOre));
    assert(ship.insert_item({x0 + 2, c - 2}, {x0 + 1, c - 2}, ItemKind::Silica));

    assert(ship.place(ItemKind::SolarFurnace, {x0, c}) == BuildResult::Ok);
    assert(ship.set_recipe({x0, c}, &recipe_named("Iron plate")));

    assert(ship.place(ItemKind::Refinery, {x0 + 5, c + 2}) == BuildResult::Ok);
    assert(ship.set_recipe({x0 + 5, c + 2}, &recipe_named("Silicon")));

    assert(ship.place(ItemKind::ExpressBelt, {x0 + 3, c + 4}, Direction::West) == BuildResult::Ok);
    assert(ship.insert_item({x0 + 3, c + 4}, {x0 + 4, c + 4}, ItemKind::Glass));

    // A junction into a router, which has nowhere to pass its item on: both
    // keep what they hold.
    assert(ship.place(ItemKind::Junction, {x0 + 6, c + 6}) == BuildResult::Ok);
    assert(ship.place(ItemKind::Router, {x0 + 7, c + 6}) == BuildResult::Ok);
    assert(ship.insert_item({x0 + 7, c + 6}, {x0 + 8, c + 6}, ItemKind::Gear));
    for (int i = 0; i < 2; ++i)
        assert(ship.insert_item({x0 + 6, c + 6}, {x0 + 5, c + 6}, ItemKind::Steel));

    // Long enough for the first catch to ride the belts into the cargo hold.
    run(world, 250);
    // Fed only now, so both machines are mid-craft when the game is saved.
    for (int i = 0; i < 2; ++i)
        assert(ship.insert_item({x0, c}, {x0 - 1, c}, ItemKind::IronOre));
    for (int i = 0; i < 2; ++i) {
        assert(ship.insert_item({x0 + 5, c + 2}, {x0 + 4, c + 2}, ItemKind::Silica));
        assert(ship.insert_item({x0 + 5, c + 2}, {x0 + 4, c + 2}, ItemKind::Carbon));
    }
    assert(ship.get_bridge().queue_craft(recipe_named("Gear")));
    assert(ship.get_bridge().queue_craft(recipe_named("Gear")));
    run(world, 10);
    return world;
}

// Every item has its own saved name, so each one loads back as what was
// saved; so does every recipe.
void test_names() {
    World world;
    Inventory& inv = world.get_ship().inventory();
    inv = Inventory{};
    for (int i = 0; i < kItemKindCount; ++i)
        inv.add(static_cast<ItemKind>(i), i + 1);
    std::string error;
    const std::optional<LoadedSave> loaded = load_text(save_text(world), error);
    assert(loaded);
    for (int i = 0; i < kItemKindCount; ++i)
        assert(loaded->world.get_ship().inventory().count(static_cast<ItemKind>(i)) == i + 1);

    std::set<std::string> recipes;
    for (const Recipe& r : all_recipes())
        assert(recipes.insert(recipe_name(r)).second);
}

// Save, load and save again: the same text, so nothing was lost.
void test_round_trip() {
    const World world = busy_world();
    const Ship& ship = world.get_ship();
    // Everything is mid-flight.
    assert(ship.get_bridge().craft_queue().size() == 2);
    assert(dynamic_cast<const Machine&>(*ship.building_at({x0, c})).crafting());
    assert(dynamic_cast<const Machine&>(*ship.building_at({x0 + 5, c + 2})).crafting());
    assert(dynamic_cast<const CargoHold&>(*ship.building_at({x0 + 5, c - 6})).get_stored().total() > 0);

    const std::string text = save_text(world);
    std::string error;
    const std::optional<LoadedSave> loaded = load_text(text, error);
    assert(loaded && error.empty());
    assert(loaded->header.name == "Test ship");
    assert(loaded->header.saved_at == 1'700'000'000);
    assert(loaded->header.played == world.get_tick());
    assert(loaded->world.get_tick() == world.get_tick());
    assert(save_text(loaded->world) == text);

    // Spot checks through the public API.
    const Ship& copy = loaded->world.get_ship();
    assert(copy.hull_at({x0 + 6, c + 3}) == ItemKind::ReinforcedHull);
    assert(copy.get_bridge().get_inventory().count(ItemKind::Hull) == ship.inventory().count(ItemKind::Hull));
    const auto* sorter = dynamic_cast<const Sorter*>(copy.building_at({x0 + 3, c - 2}));
    assert(sorter && sorter->get_filter() == ItemKind::IronOre);
    const auto* belt = dynamic_cast<const Conveyor*>(copy.building_at({x0 + 3, c + 4}));
    assert(belt && belt->facing() == Direction::West && belt->get_items().size() == 1);
    const auto* junction = dynamic_cast<const Junction*>(copy.building_at({x0 + 6, c + 6}));
    assert(junction && junction->lane(Direction::East).size() == 2);
    const auto* router = dynamic_cast<const Router*>(copy.building_at({x0 + 7, c + 6}));
    assert(router && router->get_held() == ItemKind::Gear && router->get_came_from() == Direction::East);
}

// A loaded game carries on exactly as the original does.
void test_determinism() {
    World world = busy_world();
    std::string error;
    std::optional<LoadedSave> loaded = load_text(save_text(world), error);
    assert(loaded);
    run(world, 1'000);
    run(loaded->world, 1'000);
    assert(save_text(loaded->world) == save_text(world));
}

void test_header_only() {
    World world;
    run(world, 42);
    std::istringstream in(save_text(world));
    std::string error;
    const std::optional<SaveHeader> header = read_save_header(in, error);
    assert(header && header->name == "Test ship" && header->played == 42);
}

Json& hull(Json& save) { return save["world"]["ship"]["hull"]; }
Json& buildings(Json& save) { return save["world"]["ship"]["buildings"]; }
Json& bridge(Json& save) { return buildings(save)[0]; }

// Hull tiles on the square of side `side` from (x, y).
void add_hull(Json& save, int x, int y, int side) {
    for (int dy = 0; dy < side; ++dy)
        for (int dx = 0; dx < side; ++dx)
            hull(save).push_back({{"x", x + dx}, {"y", y + dy}, {"kind", "Hull"}});
}

// A hand-written save: just the bridge at (64, 64), on the 6x6 of hull it
// covers (its top-left tile first).
static_assert(kBridgeSize == 6, "the hand-written saves below place things around a 6x6 bridge");
Json minimal() {
    Json save = Json::parse(R"({
        "format": "ore-in-orbit-save", "version": 0, "name": "T", "saved_at": 0, "played": 0,
        "world": {"tick": 0, "ship": {
            "hull": [],
            "buildings": [
                {"kind": "Bridge", "x": 64, "y": 64, "facing": "North", "output_side": 0,
                 "inventory": {}, "queue": [], "milestone": 0, "delivered": {}}
            ]}}})");
    save["version"] = kSaveVersion;
    add_hull(save, 64, 64, 6);
    return save;
}

bool loads(const std::string& text) {
    std::string error;
    const bool ok = load_text(text, error).has_value();
    assert(ok == error.empty());
    return ok;
}

void refused(const std::string& text, const char* what) {
    if (loads(text)) {
        std::printf("test_save_file: loaded a save with %s\n", what);
        assert(false);
    }
}

// A copy of `save` changed by `edit`, refused for `what`.
template <typename Edit>
void refused_after(const Json& save, const char* what, Edit edit) {
    Json changed = save;
    edit(changed);
    refused(changed.dump(), what);
}

void test_refused() {
    assert(loads(minimal().dump()));

    refused("", "nothing in it");
    refused("hello world", "no JSON");
    refused("[1, 2, 3]", "the wrong JSON");
    const std::string text = minimal().dump();
    refused(text.substr(0, text.size() - 1), "the end cut off");

    const Json save = minimal();
    refused_after(save, "the wrong format", [](Json& j) { j["format"] = "something-else"; });
    refused_after(save, "another format version", [](Json& j) { j["version"] = 99; });
    refused_after(save, "no name", [](Json& j) { j.erase("name"); });
    refused_after(save, "a name that isn't text", [](Json& j) { j["name"] = 42; });
    refused_after(save, "a negative play time", [](Json& j) { j["played"] = -1; });
    refused_after(save, "no world", [](Json& j) { j.erase("world"); });
    refused_after(save, "an unknown item", [](Json& j) { bridge(j)["inventory"] = {{"Unobtainium", 5}}; });
    refused_after(save, "a display name for an item", [](Json& j) { bridge(j)["inventory"] = {{"Iron plate", 5}}; });
    refused_after(save, "a negative count", [](Json& j) { bridge(j)["inventory"] = {{"Hull", -5}}; });
    refused_after(save, "a fractional count", [](Json& j) { bridge(j)["inventory"] = {{"Hull", 1.5}}; });
    refused_after(save, "an absurd count", [](Json& j) { bridge(j)["inventory"] = {{"Hull", 1e30}}; });
    refused_after(save, "a count as text", [](Json& j) { bridge(j)["inventory"] = {{"Hull", "5"}}; });
    refused_after(save, "an unknown recipe", [](Json& j) {
        bridge(j)["queue"] = Json::parse(R"([{"recipe": "Unobtainium", "elapsed": 0}])");
    });
    refused_after(save, "a recipe that isn't unlocked yet", [](Json& j) {
        bridge(j)["queue"] = Json::parse(R"([{"recipe": "Advanced circuit", "elapsed": 0}])");
    });
    refused_after(save, "a milestone past the last", [](Json& j) { bridge(j)["milestone"] = 99; });
    refused_after(save, "no milestone", [](Json& j) { bridge(j).erase("milestone"); });
    refused_after(save, "more delivered than the milestone needs",
                  [](Json& j) { bridge(j)["delivered"] = {{"IronPlate", 51}}; });
    refused_after(save, "a delivery the milestone doesn't need",
                  [](Json& j) { bridge(j)["delivered"] = {{"Gear", 1}}; });
    refused_after(save, "a recipe the bridge can't make", [](Json& j) {
        bridge(j)["queue"] = Json::parse(R"([{"recipe": "Glass", "elapsed": 0}])");
    });
    refused_after(save, "a craft past its end", [](Json& j) {
        bridge(j)["queue"] = Json::parse(R"([{"recipe": "Gear", "elapsed": 100000}])");
    });
    refused_after(save, "no craft queue", [](Json& j) { bridge(j).erase("queue"); });
    refused_after(save, "a hull tile that isn't hull", [](Json& j) { hull(j)[0]["kind"] = "Gear"; });
    refused_after(save, "a tile off the grid", [](Json& j) { hull(j)[0]["y"] = 200; });
    refused_after(save, "two tiles on one cell", [](Json& j) { hull(j).push_back(hull(j)[0]); });
    refused_after(save, "no bridge", [](Json& j) { buildings(j) = Json::array(); });
    refused_after(save, "the bridge off hull", [](Json& j) { hull(j)[0]["x"] = 63; });
    refused_after(save, "two bridges", [](Json& j) {
        add_hull(j, 70, 64, 6);
        Json second = bridge(j);
        second["x"] = 70;
        buildings(j).push_back(second);
    });

    // Buildings after the bridge: one more hull tile, east of it at
    // (70, 64), to put them on.
    Json one_more = minimal();
    hull(one_more).push_back({{"x", 70}, {"y", 64}, {"kind", "Hull"}});
    auto with_building = [&](const char* building) {
        Json j = one_more;
        buildings(j).push_back(Json::parse(building));
        return j.dump();
    };
    assert(loads(with_building(
        R"({"kind": "TransportBelt", "x": 70, "y": 64, "facing": "East", "output_side": 0,
            "items": [{"item": "IronOre", "pos": 30}]})")));
    refused(with_building(R"({"kind": "TransportBelt", "x": 64, "y": 64, "facing": "East", "output_side": 0,
                              "items": []})"),
            "overlapping buildings");
    refused(with_building(R"({"kind": "TransportBelt", "x": 70, "y": 64, "facing": "Up", "output_side": 0,
                              "items": []})"),
            "an unknown direction");
    refused(with_building(R"({"kind": "TransportBelt", "x": 70, "y": 64, "facing": "East", "output_side": 0,
                              "items": [{"item": "IronOre", "pos": 30}, {"item": "IronOre", "pos": 20}]})"),
            "belt items too close");
    refused(with_building(R"({"kind": "TransportBelt", "x": 70, "y": 64, "facing": "East", "output_side": 0,
                              "items": [{"item": "IronOre", "pos": 61}]})"),
            "a belt item off the belt");
    refused(with_building(R"({"kind": "Hull", "x": 70, "y": 64, "facing": "North", "output_side": 0})"),
            "hull as a building");
    refused(with_building(R"({"kind": "SolarFurnace", "x": 70, "y": 64, "facing": "North", "output_side": 0})"),
            "a building partly off hull");
    refused(with_building(R"({"kind": "FilterSorter", "x": 70, "y": 64, "facing": "North", "output_side": 0,
                              "filter": "Unobtainium", "next_right": false})"),
            "an unknown filter");
    refused(with_building(R"({"kind": "Junction", "x": 70, "y": 64, "facing": "North", "output_side": 0,
                              "lanes": {"North": [], "East": [{"item": "Gear", "elapsed": 9999}],
                                        "South": [], "West": []}})"),
            "an item past the end of a junction");
    refused(with_building(R"({"kind": "Junction", "x": 70, "y": 64, "facing": "North", "output_side": 0,
                              "lanes": {"North": [], "East": [], "South": []}})"),
            "a junction missing a way across");
    refused(with_building(R"({"kind": "Router", "x": 70, "y": 64, "facing": "North", "output_side": 0,
                              "held": "Gear", "came_from": "Up", "elapsed": 0, "next_out": "North"})"),
            "a router item from an unknown side");
}

// Machines check their recipe and progress, collectors their target, and
// large buildings their hull.
void test_machine_refused() {
    // A solar furnace on a 2x2 patch of hull at (65..66, 60..61).
    Json save = minimal();
    for (int y = 60; y <= 61; ++y)
        for (int x = 65; x <= 66; ++x)
            hull(save).push_back({{"x", x}, {"y", y}, {"kind", "Hull"}});
    buildings(save).push_back(Json::parse(R"({
        "kind": "SolarFurnace", "x": 65, "y": 60, "facing": "North", "output_side": 0,
        "recipe": "Iron plate", "work": 10, "input": {}, "output": {"IronPlate": 2}})"));
    assert(loads(save.dump()));
    auto furnace = [](Json& j) -> Json& { return buildings(j)[1]; };
    refused_after(save, "a recipe the machine can't run", [&](Json& j) { furnace(j)["recipe"] = "Gear"; });
    refused_after(save, "crafting without a recipe", [&](Json& j) { furnace(j)["recipe"] = nullptr; });
    refused_after(save, "craft progress past the end", [&](Json& j) { furnace(j)["work"] = 100000; });
    refused_after(save, "no output buffer", [&](Json& j) { furnace(j).erase("output"); });

    // A collector gathers only what it can be aimed at.
    Json gathering = minimal();
    for (int y = 60; y <= 61; ++y)
        for (int x = 65; x <= 66; ++x)
            hull(gathering).push_back({{"x", x}, {"y", y}, {"kind", "Hull"}});
    buildings(gathering).push_back(Json::parse(R"({
        "kind": "AsteroidCollector", "x": 65, "y": 60, "facing": "North", "output_side": 0,
        "progress": 0, "target": "Carbon", "caught": {"Carbon": 3}})"));
    assert(loads(gathering.dump()));
    auto collector = [](Json& j) -> Json& { return buildings(j)[1]; };
    refused_after(gathering, "iridium for an asteroid collector",
                  [&](Json& j) { collector(j)["target"] = "IridiumOre"; });
    refused_after(gathering, "a collector aimed at a plate", [&](Json& j) { collector(j)["target"] = "IronPlate"; });
    refused_after(gathering, "a collector with no target", [&](Json& j) { collector(j).erase("target"); });

    // A refinery needs reinforced hull, on load as when placing.
    Json refinery = minimal();
    for (int y = 60; y <= 62; ++y)
        for (int x = 65; x <= 67; ++x)
            hull(refinery).push_back({{"x", x}, {"y", y}, {"kind", "Hull"}});
    buildings(refinery).push_back(Json::parse(R"({
        "kind": "Refinery", "x": 65, "y": 60, "facing": "North", "output_side": 0,
        "recipe": null, "work": null, "input": {}, "output": {}})"));
    refused(refinery.dump(), "a refinery on plain hull");
    for (std::size_t i = 1; i < hull(refinery).size(); ++i)
        hull(refinery)[i]["kind"] = "ReinforcedHull";
    assert(loads(refinery.dump()));
}

}  // namespace

int main() {
    test_names();
    test_round_trip();
    test_determinism();
    test_header_only();
    test_refused();
    test_machine_refused();
    std::puts("test_save_file: all passed");
    return 0;
}
