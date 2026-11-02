// test_save_file.cpp — headless tests for saving a game as JSON and loading
// it back: every building's state survives the round trip, a loaded game
// carries on exactly as the original would have, and damaged or foreign
// files are refused with a reason (never an assert).
#include "persistence/save_file.h"
#include "sim/building.h"
#include "sim/fleet.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"
#include "sim/world.h"

#include "nlohmann/json.hpp"

#include <cassert>
#include <cstdio>
#include <set>
#include <sstream>
#include <string>

using Json = nlohmann::ordered_json;

namespace {

// Rows near the middle of the grid, through the bridge; and a column east
// of it, on the starting hull, with room for a 10x13 patch.
constexpr int c = kShipGridSize / 2;
const int x0 = layout_to_grid(starter_layout(), starter_layout().bridge).x + kBridgeSize + 3;
// The starting ship's first engine.
const GridPos kEngine = layout_to_grid(starter_layout(), starter_layout().engines.front());

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
// mid-craft, a west-facing express belt, a junction and a router holding
// items, and the bridge hand-crafting, part way through a milestone; an
// engine (ion thruster) part charged, an autocannon with ammunition
// and a damaged cargo hold, as a wave of enemies comes in; and a second
// ship, with cargo on its way to it.
World busy_world() {
    World world;
    world.flagship().inventory().add({{ItemKind::TransportBelt, 10},
                                      {ItemKind::ExpressBelt, 1},
                                      {ItemKind::FilterSorter, 1},
                                      {ItemKind::Junction, 1},
                                      {ItemKind::Router, 1},
                                      {ItemKind::SolarPanel, 1},
                                      {ItemKind::Autocannon, 1},
                                      {ItemKind::Refinery, 1}});
    // Two milestones done (the refinery's recipes need them), and some way
    // into the third.
    Progress& progress = world.flagship().get_bridge().get_progress();
    assert(progress.complete_current() && progress.complete_current());
    for (int i = 0; i < 3; ++i)
        assert(!progress.on_delivered(ItemKind::AdvancedCircuit));
    Ship& ship = world.flagship();

    assert(ship.place(ItemKind::AsteroidCollector, {x0, c - 6}) == BuildResult::Ok);
    assert(ship.set_collector_target({x0, c - 6}, ItemKind::Silica));
    for (int x = x0 + 2; x <= x0 + 4; ++x)
        assert(ship.place(ItemKind::TransportBelt, {x, c - 6}, Direction::East) == BuildResult::Ok);
    assert(ship.place(ItemKind::CargoHold, {x0 + 5, c - 6}) == BuildResult::Ok);
    assert(ship.place(ItemKind::SolarPanel, {x0 + 8, c - 6}) == BuildResult::Ok);
    for (int i = 0; i < 3; ++i)
        assert(ship.insert_item(kEngine, {kEngine.x, kEngine.y - 1}, ItemKind::FuelCell));
    assert(ship.place(ItemKind::Autocannon, {x0 + 2, c}) == BuildResult::Ok);
    for (int i = 0; i < 3; ++i)
        assert(ship.insert_item({x0 + 2, c}, {x0 + 2, c - 1}, ItemKind::SlugMagazine));

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

    // A second ship, renamed, and cargo on its way to it.
    Fleet& fleet = world.get_fleet();
    ship.inventory().add({{ItemKind::ShipStarterPack, 1}, {ItemKind::CargoPod, 1}});
    ship.inventory().add(starter_layout().cost);
    int launched = 0;
    assert(fleet.launch(fleet.flagship().id, starter_layout(), &launched) == FleetResult::Ok);
    assert(fleet.rename(launched, "Tender"));
    assert(fleet.send(fleet.flagship().id, launched, {{ItemKind::TransportBelt, 5}}, world.get_tick()) ==
           FleetResult::Ok);
    // A wave on its way in, and a building already hurt.
    assert(world.send_wave(fleet.flagship().id));
    assert(!ship.building_anchored_at({x0 + 5, c - 6})->take_damage(30));
    run(world, 10);
    return world;
}

// Every item has its own saved name, so each one loads back as what was
// saved; so does every recipe.
void test_names() {
    World world;
    Inventory& inv = world.flagship().inventory();
    inv = Inventory{};
    for (int i = 0; i < kItemKindCount; ++i)
        inv.add(static_cast<ItemKind>(i), i + 1);
    std::string error;
    const std::optional<LoadedSave> loaded = load_text(save_text(world), error);
    assert(loaded);
    for (int i = 0; i < kItemKindCount; ++i)
        assert(loaded->world.flagship().inventory().count(static_cast<ItemKind>(i)) == i + 1);

    std::set<std::string> recipes;
    for (const Recipe& r : all_recipes())
        assert(recipes.insert(recipe_name(r)).second);
}

// Save, load and save again: the same text, so nothing was lost.
void test_round_trip() {
    const World world = busy_world();
    const Ship& ship = world.flagship();
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
    const Ship& copy = loaded->world.flagship();
    assert(copy.get_layout() == &starter_layout());
    assert(copy.hull_count() == ship.hull_count());
    assert(copy.get_bridge().get_inventory().count(ItemKind::TransportBelt) ==
           ship.inventory().count(ItemKind::TransportBelt));
    const auto* sorter = dynamic_cast<const Sorter*>(copy.building_at({x0 + 3, c - 2}));
    assert(sorter && sorter->get_filter() == ItemKind::IronOre);
    const auto* belt = dynamic_cast<const Conveyor*>(copy.building_at({x0 + 3, c + 4}));
    assert(belt && belt->facing() == Direction::West && belt->get_items().size() == 1);
    const auto* junction = dynamic_cast<const Junction*>(copy.building_at({x0 + 6, c + 6}));
    assert(junction && junction->lane(Direction::East).size() == 2);
    const auto* router = dynamic_cast<const Router*>(copy.building_at({x0 + 7, c + 6}));
    assert(router && router->get_held() == ItemKind::Gear && router->get_came_from() == Direction::East);
    const auto* thruster = dynamic_cast<const Thruster*>(copy.building_at({kEngine.x + 1, kEngine.y + 2}));
    assert(thruster && thruster->get_fuel() == 3);
    const auto* turret = dynamic_cast<const Turret*>(copy.building_at({x0 + 3, c + 1}));
    assert(turret && turret->get_ammo() == 3);
    assert(copy.building_at({x0 + 5, c - 6})->get_hp() == max_hp_of(ItemKind::CargoHold) - 30);
    assert(loaded->world.get_fleet().flagship().battle.enemies.size() ==
           world.get_fleet().flagship().battle.enemies.size());
    assert(loaded->world.get_fleet().flagship().battle.under_attack());
    // The same star map.
    assert(loaded->world.get_map().size() == world.get_map().size());
    assert(loaded->world.get_map().at(3).name == world.get_map().at(3).name);

    // The fleet: both ships share the loaded progress.
    const Fleet& fleet = loaded->world.get_fleet();
    assert(fleet.get_ships().size() == 2);
    const FleetShip& tender = *fleet.get_ships()[1];
    assert(tender.name == "Tender");
    assert(&tender.ship.get_bridge().get_progress() == &fleet.get_progress());
    assert(&copy.get_bridge().get_progress() == &fleet.get_progress());
    assert(fleet.get_progress().delivered_count(ItemKind::AdvancedCircuit) == 3);
    assert(fleet.in_flight().size() == 1 && fleet.in_flight()[0].to == tender.id);
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

Json& fleet(Json& save) { return save["world"]["fleet"]; }
Json& sectors(Json& save) { return save["world"]["star_map"]["sectors"]; }
Json& progress(Json& save) { return fleet(save)["progress"]; }
Json& first_ship(Json& save) { return fleet(save)["ships"][0]; }
Json& ship_json(Json& save) { return first_ship(save)["ship"]; }
Json& buildings(Json& save) { return ship_json(save)["buildings"]; }
Json& bridge(Json& save) { return buildings(save)[0]; }
// The starter layout's engines, after the bridge.
Json& engine(Json& save, std::size_t i) { return buildings(save)[1 + i]; }

// A hand-written save: a ship in the starter layout, with just its bridge
// and engines, and the buildings below placed east and north-east of the
// bridge, on its hull.
static_assert(kBridgeSize == 6, "the hand-written saves below place things around a 6x6 bridge");
Json minimal() {
    Json save = Json::parse(R"({
        "format": "ore-in-orbit-save", "version": 0, "name": "T", "saved_at": 0, "played": 0,
        "world": {"tick": 100, "rng": 7,
          "star_map": {"sectors": [
            {"name": "Home", "x": 0, "y": 0, "neighbors": [1],
             "yields": {"IronOre": 100, "CopperOre": 100, "Silica": 100, "Carbon": 100, "IridiumOre": 100},
             "threat": 0, "waves_left": 0, "visited": true, "surveyed": true},
            {"name": "Next door", "x": 10, "y": -5.5, "neighbors": [0],
             "yields": {"IronOre": 0, "CopperOre": 150, "Silica": 100, "Carbon": 100, "IridiumOre": 0},
             "threat": 1, "waves_left": 4, "visited": false, "surveyed": false}]},
          "fleet": {
            "next_ship_id": 2,
            "progress": {"milestone": 0, "delivered": {}},
            "ships": [{"id": 1, "name": "Flagship", "sector": 0, "ship": {
                "layout": "skiff",
                "buildings": [
                    {"kind": "Bridge", "x": 0, "y": 0, "facing": "North", "output_side": 0,
                     "inventory": {}, "queue": []}
                ]},
                "battle": {"enemies": [], "next_wave": null}}],
            "transfers": []}}})");
    save["version"] = kSaveVersion;
    const ShipLayout& layout = starter_layout();
    const GridPos at = layout_to_grid(layout, layout.bridge);
    assert(at == (GridPos{61, 61}));  // the positions below assume it
    bridge(save)["x"] = at.x;
    bridge(save)["y"] = at.y;
    for (const GridPos e : layout.engines) {
        const GridPos p = layout_to_grid(layout, e);
        buildings(save).push_back(
            {{"kind", "IonThruster"}, {"x", p.x}, {"y", p.y}, {"facing", "North"}, {"output_side", 0}, {"fuel", 0}});
    }
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
    refused_after(save, "a negative count", [](Json& j) { bridge(j)["inventory"] = {{"Gear", -5}}; });
    refused_after(save, "a fractional count", [](Json& j) { bridge(j)["inventory"] = {{"Gear", 1.5}}; });
    refused_after(save, "an absurd count", [](Json& j) { bridge(j)["inventory"] = {{"Gear", 1e30}}; });
    refused_after(save, "a count as text", [](Json& j) { bridge(j)["inventory"] = {{"Gear", "5"}}; });
    refused_after(save, "hull, no longer an item", [](Json& j) { bridge(j)["inventory"] = {{"Hull", 5}}; });
    refused_after(save, "an unknown recipe", [](Json& j) {
        bridge(j)["queue"] = Json::parse(R"([{"recipe": "Unobtainium", "elapsed": 0}])");
    });
    refused_after(save, "a recipe that isn't unlocked yet", [](Json& j) {
        bridge(j)["queue"] = Json::parse(R"([{"recipe": "Advanced circuit", "elapsed": 0}])");
    });
    refused_after(save, "a milestone past the last", [](Json& j) { progress(j)["milestone"] = 99; });
    refused_after(save, "no milestone", [](Json& j) { progress(j).erase("milestone"); });
    refused_after(save, "more delivered than the milestone needs",
                  [](Json& j) { progress(j)["delivered"] = {{"IronPlate", 51}}; });
    refused_after(save, "a delivery the milestone doesn't need",
                  [](Json& j) { progress(j)["delivered"] = {{"Gear", 1}}; });
    refused_after(save, "a recipe the bridge can't make", [](Json& j) {
        bridge(j)["queue"] = Json::parse(R"([{"recipe": "Glass", "elapsed": 0}])");
    });
    refused_after(save, "a craft past its end", [](Json& j) {
        bridge(j)["queue"] = Json::parse(R"([{"recipe": "Gear", "elapsed": 100000}])");
    });
    refused_after(save, "no craft queue", [](Json& j) { bridge(j).erase("queue"); });
    refused_after(save, "no layout", [](Json& j) { ship_json(j).erase("layout"); });
    refused_after(save, "an unknown layout", [](Json& j) { ship_json(j)["layout"] = "battlestar"; });
    refused_after(save, "hull tiles instead of a layout", [](Json& j) {
        ship_json(j).erase("layout");
        ship_json(j)["hull"] = Json::array();
    });
    refused_after(save, "no bridge", [](Json& j) { buildings(j).erase(0); });
    refused_after(save, "the bridge away from its layout's place", [](Json& j) { bridge(j)["x"] = 62; });
    refused_after(save, "the bridge off hull", [](Json& j) { bridge(j)["y"] = 10; });
    refused_after(save, "an engine missing", [](Json& j) { buildings(j).erase(1); });
    refused_after(save, "an engine moved", [](Json& j) { engine(j, 0)["x"] = 56; });
    refused_after(save, "an extra engine", [](Json& j) {
        buildings(j).push_back(engine(j, 0));
        buildings(j).back()["x"] = 70;
        buildings(j).back()["y"] = 64;
    });
    refused_after(save, "an overfull engine", [](Json& j) { engine(j, 0)["fuel"] = kThrusterCharge + 1; });
    refused_after(save, "no ships", [](Json& j) { fleet(j)["ships"] = Json::array(); });
    refused_after(save, "a ship id not given out yet", [](Json& j) { first_ship(j)["id"] = 2; });
    refused_after(save, "a ship id of 0", [](Json& j) { first_ship(j)["id"] = 0; });
    refused_after(save, "a ship with no name", [](Json& j) { first_ship(j).erase("name"); });
    refused_after(save, "a ship in a sector that doesn't exist", [](Json& j) { first_ship(j)["sector"] = 5; });
    refused_after(save, "two ships with one id", [](Json& j) {
        fleet(j)["next_ship_id"] = 3;
        fleet(j)["ships"].push_back(first_ship(j));
    });
    refused_after(save, "no transfers list", [](Json& j) { fleet(j).erase("transfers"); });
    refused_after(save, "a ship in a sector never visited", [](Json& j) { first_ship(j)["sector"] = 1; });
    refused_after(save, "no star map", [](Json& j) { j["world"].erase("star_map"); });
    refused_after(save, "no sectors", [](Json& j) { sectors(j) = Json::array(); });
    refused_after(save, "a link to a sector that isn't there", [](Json& j) { sectors(j)[0]["neighbors"] = {1, 2}; });
    refused_after(save, "a link only one way", [](Json& j) { sectors(j)[1]["neighbors"] = Json::array(); });
    refused_after(save, "a sector out of reach", [](Json& j) {
        sectors(j)[0]["neighbors"] = Json::array();
        sectors(j)[1]["neighbors"] = Json::array();
    });
    refused_after(save, "a sector linked to itself", [](Json& j) { sectors(j)[0]["neighbors"] = {0, 1}; });
    refused_after(save, "a sector linked twice", [](Json& j) {
        sectors(j)[0]["neighbors"] = {1, 1};
        sectors(j)[1]["neighbors"] = {0, 0};
    });
    refused_after(save, "a yield for a plate", [](Json& j) { sectors(j)[1]["yields"]["IronPlate"] = 100; });
    refused_after(save, "a missing yield", [](Json& j) { sectors(j)[1]["yields"].erase("Carbon"); });
    refused_after(save, "a negative yield", [](Json& j) { sectors(j)[1]["yields"]["Carbon"] = -1; });
    refused_after(save, "a threat past the highest", [](Json& j) { sectors(j)[1]["threat"] = kMaxThreat + 1; });
    refused_after(save, "a sector at infinity", [](Json& j) { sectors(j)[1]["x"] = 1e300; });
    refused_after(save, "a visited sector not surveyed", [](Json& j) { sectors(j)[1]["visited"] = true; });
    refused_after(save, "more waves than the threat sends", [](Json& j) { sectors(j)[1]["waves_left"] = 99; });
    refused_after(save, "no random number state", [](Json& j) { j["world"].erase("rng"); });
    refused_after(save, "the bridge at no health", [](Json& j) { bridge(j)["hp"] = 0; });
    refused_after(save, "the bridge past full health", [](Json& j) { bridge(j)["hp"] = 1e6; });
    refused_after(save, "no battle", [](Json& j) { first_ship(j).erase("battle"); });
    refused_after(save, "a wave that should have come", [](Json& j) { first_ship(j)["battle"]["next_wave"] = 50; });

    Json fighting = minimal();
    first_ship(fighting)["battle"]["enemies"].push_back(
        Json::parse(R"({"kind": "Drone", "x": -3.5, "y": 40, "hp": 20, "cooldown": 5, "target": {"x": 64, "y": 64}})"));
    assert(loads(fighting.dump()));
    auto enemy = [](Json& j) -> Json& { return first_ship(j)["battle"]["enemies"][0]; };
    refused_after(fighting, "an unknown enemy", [&](Json& j) { enemy(j)["kind"] = "Kraken"; });
    refused_after(fighting, "an enemy past full health", [&](Json& j) { enemy(j)["hp"] = 31; });
    refused_after(fighting, "a dead enemy", [&](Json& j) { enemy(j)["hp"] = 0; });
    refused_after(fighting, "an enemy far beyond the map", [&](Json& j) { enemy(j)["x"] = 1e9; });
    refused_after(fighting, "an enemy aiming off the grid", [&](Json& j) { enemy(j)["target"]["x"] = 500; });
    refused_after(fighting, "an enemy cooling down too long", [&](Json& j) { enemy(j)["cooldown"] = 1000; });
    refused_after(save, "two bridges", [](Json& j) {
        Json second = bridge(j);
        second["x"] = 70;
        buildings(j).push_back(second);
    });

    // Buildings after the bridge and engines: east of it at (70, 64), on
    // the hull.
    const Json one_more = minimal();
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
    refused(with_building(R"({"kind": "SolarFurnace", "x": 83, "y": 64, "facing": "North", "output_side": 0})"),
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

// Cargo in flight names ships by id, and lands after the save's tick, by
// no more than the time it takes.
void test_transfers_refused() {
    Json save = minimal();
    fleet(save)["transfers"].push_back(Json::parse(R"({"from": 1, "to": 1, "items": {"Gear": 5}, "arrives": 200})"));
    assert(loads(save.dump()));
    auto transfer = [](Json& j) -> Json& { return fleet(j)["transfers"][0]; };
    refused_after(save, "cargo to a ship id not given out yet", [&](Json& j) { transfer(j)["to"] = 7; });
    refused_after(save, "cargo that should have landed", [&](Json& j) { transfer(j)["arrives"] = 100; });
    refused_after(save, "cargo that takes too long", [&](Json& j) { transfer(j)["arrives"] = 100 + kTransferTicks + 1; });
    refused_after(save, "cargo with an unknown item", [&](Json& j) { transfer(j)["items"] = {{"Unobtainium", 1}}; });
}

// Machines check their recipe and progress, collectors their target, and
// turrets their ammunition.
void test_machine_refused() {
    // A solar furnace at (70..71, 58..59).
    Json save = minimal();
    buildings(save).push_back(Json::parse(R"({
        "kind": "SolarFurnace", "x": 70, "y": 58, "facing": "North", "output_side": 0,
        "recipe": "Iron plate", "work": 10, "input": {}, "output": {"IronPlate": 2}})"));
    assert(loads(save.dump()));
    auto furnace = [](Json& j) -> Json& { return buildings(j).back(); };
    refused_after(save, "a recipe the machine can't run", [&](Json& j) { furnace(j)["recipe"] = "Gear"; });
    refused_after(save, "crafting without a recipe", [&](Json& j) { furnace(j)["recipe"] = nullptr; });
    refused_after(save, "craft progress past the end", [&](Json& j) { furnace(j)["work"] = 100000; });
    refused_after(save, "no output buffer", [&](Json& j) { furnace(j).erase("output"); });

    // A collector gathers only what it can be aimed at.
    Json gathering = minimal();
    buildings(gathering).push_back(Json::parse(R"({
        "kind": "AsteroidCollector", "x": 70, "y": 58, "facing": "North", "output_side": 0,
        "progress": 0, "target": "Carbon", "caught": {"Carbon": 3}})"));
    assert(loads(gathering.dump()));
    auto collector = [](Json& j) -> Json& { return buildings(j).back(); };
    refused_after(gathering, "iridium for an asteroid collector",
                  [&](Json& j) { collector(j)["target"] = "IridiumOre"; });
    refused_after(gathering, "a collector aimed at a plate", [&](Json& j) { collector(j)["target"] = "IronPlate"; });
    refused_after(gathering, "a collector with no target", [&](Json& j) { collector(j).erase("target"); });

    // A turret's ammunition: never a full item loaded (it's loaded when
    // needed).
    Json armed = minimal();
    buildings(armed).push_back(Json::parse(R"({
        "kind": "Autocannon", "x": 70, "y": 58, "facing": "North", "output_side": 0,
        "ammo": 4, "shots": 7, "cooldown": 3})"));
    assert(loads(armed.dump()));
    auto turret = [](Json& j) -> Json& { return buildings(j).back(); };
    refused_after(armed, "a full item loaded", [&](Json& j) { turret(j)["shots"] = 8; });
    refused_after(armed, "too much ammunition", [&](Json& j) { turret(j)["ammo"] = kTurretAmmoCapacity + 1; });
    refused_after(armed, "a turret cooling down too long", [&](Json& j) { turret(j)["cooldown"] = 1000; });
}

// A tunnel saved with an item partway under comes back linked, and the item
// carries on, however far past a belt's length it had got.
void test_tunnel_round_trip() {
    World world;
    Ship& ship = world.flagship();
    ship.inventory().add(ItemKind::BeltTunnel, 2);
    const GridPos entrance{x0, c + 8};
    const GridPos exit{x0 + 4, c + 8};
    const GridPos hold{x0 + 5, c + 8};
    assert(ship.place(ItemKind::BeltTunnel, entrance, Direction::East) == BuildResult::Ok);
    assert(ship.place(ItemKind::BeltTunnel, exit, Direction::East) == BuildResult::Ok);
    assert(ship.place(ItemKind::CargoHold, hold) == BuildResult::Ok);
    assert(ship.insert_item(entrance, {x0 - 1, c + 8}, ItemKind::Gear));
    run(world, 40);
    const auto* tunnel = dynamic_cast<const Tunnel*>(ship.building_at(entrance));
    assert(tunnel->get_items().front().pos > 2 * kBeltLength);

    const std::string text = save_text(world);
    std::string error;
    std::optional<LoadedSave> loaded = load_text(text, error);
    assert(loaded && error.empty());
    assert(save_text(loaded->world) == text);
    const Ship& copy = loaded->world.flagship();
    const auto* in = dynamic_cast<const Tunnel*>(copy.building_at(entrance));
    assert(in && in->get_exit() == exit && in->get_items().size() == 1);
    run(loaded->world, 100);
    assert(dynamic_cast<const CargoHold&>(*copy.building_at(hold)).get_stored().count(ItemKind::Gear) == 1);
}

}  // namespace

int main() {
    test_names();
    test_round_trip();
    test_tunnel_round_trip();
    test_determinism();
    test_header_only();
    test_refused();
    test_transfers_refused();
    test_machine_refused();
    std::puts("test_save_file: all passed");
    return 0;
}
