// save_file.cpp — the save format: the World as JSON (vendored
// nlohmann::json, third_party/nlohmann/), and reading it back (see
// save_file.h).
//
// Everything about saving lives here, not in the simulation: SaveAccess, at
// the bottom, is the one struct the simulation classes open their private
// state to (their `friend struct SaveAccess`).
//
// Items, directions and recipes are written by name (see json_io.h, which
// holds the reading and writing pieces shared with blueprints).
//
// Reading checks everything that could break the simulation's invariants
// (ranges, known names, bounds, hull under buildings, overlaps, one bridge).
// A problem throws LoadError, or nlohmann's json::exception for a missing
// key or a value of the wrong type; read_save() turns either into `error`.
// A bad save never asserts.
#include "persistence/save_file.h"

#include "persistence/json_io.h"
#include "sim/building.h"
#include "sim/fleet.h"
#include "sim/milestones.h"
#include "sim/recipes.h"
#include "sim/sector.h"
#include "sim/ship.h"
#include "sim/world.h"

#include <algorithm>
#include <cmath>
#include <istream>
#include <limits>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace json_io;

namespace {

constexpr const char* kFormat = "ore-in-orbit-save";
// What a save calls the bridge, which has no ItemKind.
constexpr const char* kBridgeId = "Bridge";

// The most of one item a saved inventory may hold: little enough that a
// whole inventory's total() can't overflow.
constexpr int kMaxSavedCount = std::numeric_limits<int>::max() / kItemKindCount;
// The longest saved craft queue: far beyond play, but it keeps a damaged
// save from asking for an absurd amount.
constexpr std::size_t kMaxSavedQueue = 100'000;

// ---------------------------------------------------------------------------
// Checked reading (the rest is in json_io.h)
// ---------------------------------------------------------------------------

Tick load_tick(const Json& j, Tick hi = std::numeric_limits<Tick>::max()) {
    if (!j.is_number_unsigned() || j.get<std::uint64_t>() > hi)
        throw LoadError("expected a tick count up to " + std::to_string(hi) + ", not " + j.dump());
    return j.get<Tick>();
}

// A tick count that a counter resets from when it reaches `ticks` (so at
// most ticks - 1).
Tick load_progress(const Json& j, Tick ticks) {
    return load_tick(j, ticks > 0 ? ticks - 1 : 0);
}

int load_coord(const Json& j) {
    return static_cast<int>(load_int(j, 0, kShipGridSize - 1));
}

// A number from -limit to limit.
float load_float(const Json& j, float limit) {
    if (!j.is_number() || !std::isfinite(j.get<double>()) || std::abs(j.get<double>()) > limit)
        throw LoadError("expected a number up to " + std::to_string(limit) + " either way, not " + j.dump());
    return j.get<float>();
}

// An inventory: an object of counts by item, {"IronPlate": 200, ...}.
Json save_inventory(const Inventory& inv) {
    Json j = Json::object();
    for (const ItemStack& s : inv.stacks())
        j[item_id(s.kind)] = s.count;
    return j;
}

Inventory load_inventory(const Json& j) {
    if (!j.is_object())
        throw LoadError("expected an object of item counts, not " + j.dump());
    Inventory inv;
    for (const auto& [id, count] : j.items())
        inv.add(load_item(Json(id)), static_cast<int>(load_int(count, 0, kMaxSavedCount)));
    return inv;
}

}  // namespace

// ---------------------------------------------------------------------------
// The simulation's state
// ---------------------------------------------------------------------------

// The private state of the simulation classes, which they open to this one
// struct (see their `friend struct SaveAccess`).
struct SaveAccess {
    // A building's own state, added to its object `j` (after its kind and
    // position).
    static void save_state(Json& j, const Building& b) {
        if (const auto* bridge = dynamic_cast<const Bridge*>(&b)) {
            j["inventory"] = save_inventory(bridge->inventory);
            Json jobs = Json::array();
            for (const CraftJob& job : bridge->queue)
                jobs.push_back({{"recipe", save_recipe(job.recipe)}, {"elapsed", job.elapsed}});
            j["queue"] = std::move(jobs);
        } else if (const auto* c = dynamic_cast<const Collector*>(&b)) {
            j["progress"] = c->progress;
            j["target"] = item_id(c->target);
            j["caught"] = save_inventory(c->caught);
        } else if (const auto* belt = dynamic_cast<const Conveyor*>(&b)) {
            Json items = Json::array();
            for (const BeltItem& item : belt->get_items())
                items.push_back({{"item", item_id(item.kind)}, {"pos", item.pos}});
            j["items"] = std::move(items);
        } else if (const auto* sorter = dynamic_cast<const Sorter*>(&b)) {
            j["filter"] = save_optional_item(sorter->get_filter());
            j["next_right"] = sorter->next_right;
        } else if (const auto* junction = dynamic_cast<const Junction*>(&b)) {
            // The items crossing each way, front first.
            Json lanes = Json::object();
            for (int d = 0; d < 4; ++d) {
                Json lane = Json::array();
                for (const JunctionItem& item : junction->lanes[static_cast<std::size_t>(d)])
                    lane.push_back({{"item", item_id(item.kind)}, {"elapsed", item.elapsed}});
                lanes[direction_id(static_cast<Direction>(d))] = std::move(lane);
            }
            j["lanes"] = std::move(lanes);
        } else if (const auto* router = dynamic_cast<const Router*>(&b)) {
            j["held"] = save_optional_item(router->held);
            j["came_from"] = direction_id(router->came_from);
            j["elapsed"] = router->elapsed;
            j["next_out"] = direction_id(router->next_out);
        } else if (const auto* hold = dynamic_cast<const CargoHold*>(&b)) {
            j["stored"] = save_inventory(hold->stored);
        } else if (const auto* thruster = dynamic_cast<const Thruster*>(&b)) {
            j["fuel"] = thruster->fuel;
        } else if (const auto* m = dynamic_cast<const Machine*>(&b)) {
            j["recipe"] = save_recipe(m->recipe);
            // How far along the craft under way is, or null when idle.
            j["work"] = m->work ? Json(*m->work) : Json(nullptr);
            j["input"] = save_inventory(m->input);
            j["output"] = save_inventory(m->output);
        }
        // Power plants and survey scanners hold no state.
    }

    // The fleet's milestone progress: the milestone being worked on, and
    // what has been delivered toward it, as item counts.
    static Json save_progress(const Progress& progress) {
        Json delivered = Json::object();
        for (int i = 0; i < kItemKindCount; ++i)
            if (const int n = progress.delivered[static_cast<std::size_t>(i)]; n > 0)
                delivered[item_id(static_cast<ItemKind>(i))] = n;
        return {{"milestone", progress.current}, {"delivered", std::move(delivered)}};
    }

    static Progress load_progress_of(const Json& j) {
        Progress progress;
        progress.current = static_cast<std::size_t>(
            load_int(j.at("milestone"), 0, static_cast<std::int64_t>(all_milestones().size())));
        const Inventory delivered = load_inventory(j.at("delivered"));
        const Milestone* milestone = progress.current_milestone();
        for (const ItemStack& s : delivered.stacks()) {
            int need = 0;
            if (milestone)
                for (const ItemStack& d : milestone->deliver)
                    if (d.kind == s.kind)
                        need = d.count;
            if (s.count > need)
                throw LoadError(std::string("more ") + item_id(s.kind) + " delivered than the milestone needs");
            progress.delivered[static_cast<std::size_t>(s.kind)] = s.count;
        }
        return progress;
    }

    // Reads a building's own state from `j` into the new building `b`. A
    // bridge already follows the fleet's progress (loaded first: the craft
    // queue may only hold unlocked recipes).
    static void load_state(const Json& j, Building& b) {
        if (auto* bridge = dynamic_cast<Bridge*>(&b)) {
            bridge->inventory = load_inventory(j.at("inventory"));
            const Progress& progress = *bridge->progress;
            const Json& jobs = load_array(j.at("queue"), "crafts");
            if (jobs.size() > kMaxSavedQueue)
                throw LoadError("too many crafts queued");
            for (const Json& job : jobs) {
                const Recipe* recipe = load_recipe(job.at("recipe"));
                if (!recipe || !recipe->hand_craftable || !progress.unlocked(*recipe))
                    throw LoadError("the bridge can't craft " + job.at("recipe").dump());
                // A job finishes (and leaves the queue) when it reaches the
                // recipe's ticks.
                bridge->queue.push_back({recipe, load_progress(job.at("elapsed"), recipe->ticks)});
            }
        } else if (auto* c = dynamic_cast<Collector*>(&b)) {
            // A catch takes progress back down when it reaches the
            // interval (in hundredths of a tick).
            c->progress = load_progress(j.at("progress"), c->spec->interval * 100);
            const ItemKind target = load_item(j.at("target"));
            if (!c->can_target(target))
                throw LoadError(std::string(item_id(*c->item_kind())) + " can't gather " + item_id(target));
            c->target = target;
            c->caught = load_inventory(j.at("caught"));
        } else if (auto* belt = dynamic_cast<Conveyor*>(&b)) {
            for (const Json& item : load_array(j.at("items"), "belt items")) {
                const ItemKind kind = load_item(item.at("item"));
                const int pos = static_cast<int>(load_int(item.at("pos"), 0, kBeltLength));
                if (!belt->has_room_at(pos))
                    throw LoadError("belt items too close together");
                belt->insert(kind, pos);
            }
        } else if (auto* sorter = dynamic_cast<Sorter*>(&b)) {
            sorter->set_filter(load_optional_item(j.at("filter")));
            sorter->next_right = j.at("next_right").get<bool>();
        } else if (auto* junction = dynamic_cast<Junction*>(&b)) {
            const Json& lanes = j.at("lanes");
            for (int d = 0; d < 4; ++d) {
                const Json& lane = load_array(lanes.at(direction_id(static_cast<Direction>(d))), "junction items");
                if (lane.size() > kJunctionCapacity)
                    throw LoadError("too many items in a junction");
                for (const Json& item : lane)
                    junction->lanes[static_cast<std::size_t>(d)].push_back(
                        {load_item(item.at("item")), load_tick(item.at("elapsed"), kJunctionTicks)});
            }
        } else if (auto* router = dynamic_cast<Router*>(&b)) {
            router->held = load_optional_item(j.at("held"));
            router->came_from = load_direction(j.at("came_from"));
            router->elapsed = load_tick(j.at("elapsed"), kRouterTicks);
            router->next_out = load_direction(j.at("next_out"));
        } else if (auto* hold = dynamic_cast<CargoHold*>(&b)) {
            hold->stored = load_inventory(j.at("stored"));
        } else if (auto* thruster = dynamic_cast<Thruster*>(&b)) {
            thruster->fuel = static_cast<int>(load_int(j.at("fuel"), 0, kThrusterCharge));
        } else if (auto* m = dynamic_cast<Machine*>(&b)) {
            const Recipe* recipe = load_recipe(j.at("recipe"));
            if (recipe && !m->can_run(*recipe))
                throw LoadError(std::string(item_id(*m->item_kind())) + " can't run " + recipe_name(*recipe));
            m->recipe = recipe;
            if (const Json& work = j.at("work"); !work.is_null()) {
                if (!recipe)
                    throw LoadError("crafting without a recipe");
                // A craft finishes (and work resets) when it reaches the
                // recipe's ticks.
                m->work = load_progress(work, recipe->ticks);
            }
            m->input = load_inventory(j.at("input"));
            m->output = load_inventory(j.at("output"));
        }
    }

    // The hull tiles, then every building in placement order (which is also
    // the order they tick in).
    static Json save_ship(const Ship& ship) {
        Json tiles = Json::array();
        for (int y = 0; y < kShipGridSize; ++y)
            for (int x = 0; x < kShipGridSize; ++x)
                if (const std::optional<ItemKind> tile = ship.hull_at({x, y}))
                    tiles.push_back({{"x", x}, {"y", y}, {"kind", item_id(*tile)}});

        Json buildings = Json::array();
        for (const GridPos p : ship.placed) {
            const Building& b = *ship.building_anchored_at(p);
            const std::optional<ItemKind> kind = b.item_kind();
            Json j = {
                {"kind", kind ? item_id(*kind) : kBridgeId},
                {"x", p.x},
                {"y", p.y},
                {"facing", direction_id(b.facing())},
                {"output_side", b.next_output_side},
            };
            save_state(j, b);
            buildings.push_back(std::move(j));
        }
        return {{"hull", std::move(tiles)}, {"buildings", std::move(buildings)}};
    }

    // Rebuilds the grids through Ship::add_building, so the anchor and
    // placement order come out as placing them would make them, checking the
    // placement rules a game can't break. Its bridge follows `progress`.
    static Ship load_ship(const Json& j, const std::shared_ptr<Progress>& progress) {
        Ship ship;
        for (const Json& t : load_array(j.at("hull"), "hull tiles")) {
            const GridPos p{load_coord(t.at("x")), load_coord(t.at("y"))};
            const ItemKind kind = load_item(t.at("kind"));
            std::optional<ItemKind>& tile = ship.hull[static_cast<std::size_t>(p.y)][static_cast<std::size_t>(p.x)];
            if (kind != ItemKind::Hull && kind != ItemKind::ReinforcedHull)
                throw LoadError(std::string("not a hull tile: ") + item_id(kind));
            if (tile)
                throw LoadError("two hull tiles on one cell");
            tile = kind;
        }
        for (const Json& b : load_array(j.at("buildings"), "buildings"))
            load_building(b, ship, progress);
        if (!ship.bridge)
            throw LoadError("the ship has no bridge");
        return ship;
    }

    static void load_building(const Json& j, Ship& ship, const std::shared_ptr<Progress>& progress) {
        const Json& kind_json = j.at("kind");
        std::unique_ptr<Building> b;
        if (kind_json == kBridgeId) {
            if (ship.bridge)
                throw LoadError("more than one bridge");
            auto bridge = std::make_unique<Bridge>();
            bridge->share_progress(progress);
            b = std::move(bridge);
        } else {
            const ItemKind kind = load_item(kind_json);
            if (!is_placeable(kind) || kind == ItemKind::Hull || kind == ItemKind::ReinforcedHull)
                throw LoadError(std::string("not a building: ") + item_id(kind));
            b = make_building(kind, load_direction(j.at("facing")));
        }
        // push_output keeps it below the number of cells around the footprint.
        b->next_output_side = static_cast<std::size_t>(load_int(j.at("output_side"), 0, 1'000));

        const GridPos p{load_coord(j.at("x")), load_coord(j.at("y"))};
        const GridSize size = b->footprint();
        if (!ship.in_bounds({p.x + size.w - 1, p.y + size.h - 1}))
            throw LoadError("a building sticks out of the grid");
        const bool reinforced = b->item_kind() && needs_reinforced_hull(*b->item_kind());
        for (int y = p.y; y < p.y + size.h; ++y)
            for (int x = p.x; x < p.x + size.w; ++x) {
                const std::optional<ItemKind> tile = ship.hull_at({x, y});
                if (!tile || (reinforced && tile != ItemKind::ReinforcedHull))
                    throw LoadError("a building without the hull it needs under it");
                if (ship.anchor_of({x, y}))
                    throw LoadError("buildings overlap");
            }

        load_state(j, *b);
        if (auto* bridge = dynamic_cast<Bridge*>(b.get()))
            ship.bridge = bridge;
        ship.add_building(p, std::move(b));
    }

    // The ships, oldest first, and the cargo in flight between them.
    static Json save_fleet(const Fleet& fleet) {
        Json ships = Json::array();
        for (const std::unique_ptr<FleetShip>& s : fleet.ships)
            ships.push_back({{"id", s->id}, {"name", s->name}, {"sector", s->sector}, {"ship", save_ship(s->ship)}});
        Json transfers = Json::array();
        for (const Transfer& t : fleet.transfers) {
            Inventory items;
            items.add(t.items);
            transfers.push_back(
                {{"from", t.from}, {"to", t.to}, {"items", save_inventory(items)}, {"arrives", t.arrives}});
        }
        return {
            {"next_ship_id", fleet.next_id},
            {"progress", save_progress(*fleet.progress)},
            {"ships", std::move(ships)},
            {"transfers", std::move(transfers)},
        };
    }

    // `now`: the World's tick, which cargo in flight lands after; ships are
    // in one of `map`'s visited sectors.
    static Fleet load_fleet(const Json& j, Tick now, const StarMap& map) {
        Fleet fleet;
        fleet.next_id = static_cast<int>(load_int(j.at("next_ship_id"), 2, 1'000'000));
        *fleet.progress = load_progress_of(j.at("progress"));
        for (const Json& s : load_array(j.at("ships"), "ships")) {
            const int id = static_cast<int>(load_int(s.at("id"), 1, fleet.next_id - 1));
            if (fleet.find(id))
                throw LoadError("two ships with id " + std::to_string(id));
            std::string name = load_string(s.at("name"), "a ship name");
            const int sector = static_cast<int>(load_int(s.at("sector"), 0, map.size() - 1));
            if (!map.at(sector).visited)
                throw LoadError("a ship in a sector never visited");
            fleet.ships.push_back(std::make_unique<FleetShip>(
                FleetShip{id, std::move(name), load_ship(s.at("ship"), fleet.progress), sector}));
        }
        if (fleet.ships.empty())
            throw LoadError("the fleet has no ships");
        for (const Json& t : load_array(j.at("transfers"), "transfers")) {
            // The ships either end may be gone (the cargo is lost when it lands).
            Transfer transfer;
            transfer.from = static_cast<int>(load_int(t.at("from"), 1, fleet.next_id - 1));
            transfer.to = static_cast<int>(load_int(t.at("to"), 1, fleet.next_id - 1));
            transfer.items = load_inventory(t.at("items")).stacks();
            transfer.arrives = load_tick(t.at("arrives"), now + kTransferTicks);
            if (transfer.arrives <= now)
                throw LoadError("cargo that should have landed already");
            fleet.transfers.push_back(std::move(transfer));
        }
        return fleet;
    }

    // Every sector, home first: where it is on the map, its links (by
    // index), its yields (by raw resource) and threat, and what the player
    // knows of it.
    static Json save_map(const StarMap& map) {
        Json sectors = Json::array();
        for (const Sector& s : map.sectors) {
            Json yields = Json::object();
            for (int r = 0; r < kRawResourceCount; ++r)
                yields[item_id(kRawResources[r])] = s.yields[static_cast<std::size_t>(r)];
            sectors.push_back({
                {"name", s.name},
                {"x", s.x},
                {"y", s.y},
                {"neighbors", s.neighbors},
                {"yields", std::move(yields)},
                {"threat", s.threat},
                {"visited", s.visited},
                {"surveyed", s.surveyed},
            });
        }
        return {{"sectors", std::move(sectors)}};
    }

    // Checks the links: both ways, none to itself or twice, and every
    // sector reachable from home.
    static StarMap load_map(const Json& j) {
        StarMap map;
        const Json& sectors = load_array(j.at("sectors"), "sectors");
        if (sectors.empty() || sectors.size() > 1'000)
            throw LoadError("expected 1 to 1000 sectors");
        const auto n = static_cast<std::int64_t>(sectors.size());
        for (const Json& js : sectors) {
            Sector s;
            s.name = load_string(js.at("name"), "a sector name");
            s.x = load_float(js.at("x"), 10'000.0f);
            s.y = load_float(js.at("y"), 10'000.0f);
            for (const Json& link : load_array(js.at("neighbors"), "sector links"))
                s.neighbors.push_back(static_cast<int>(load_int(link, 0, n - 1)));
            const Json& yields = js.at("yields");
            for (int r = 0; r < kRawResourceCount; ++r)
                s.yields[static_cast<std::size_t>(r)] =
                    static_cast<int>(load_int(yields.at(item_id(kRawResources[r])), 0, 1'000));
            if (yields.size() != static_cast<std::size_t>(kRawResourceCount))
                throw LoadError("a yield for something that isn't a raw resource");
            s.threat = static_cast<int>(load_int(js.at("threat"), 0, kMaxThreat));
            s.visited = js.at("visited").get<bool>();
            s.surveyed = js.at("surveyed").get<bool>();
            if (s.visited && !s.surveyed)
                throw LoadError("a sector visited but not surveyed");
            map.sectors.push_back(std::move(s));
        }
        std::vector<bool> reached(map.sectors.size(), false);
        std::vector<int> stack{0};
        reached[0] = true;
        for (int i = 0; i < map.size(); ++i) {
            const std::vector<int>& links = map.at(i).neighbors;
            for (std::size_t k = 0; k < links.size(); ++k) {
                if (links[k] == i || std::find(links.begin(), links.begin() + static_cast<std::ptrdiff_t>(k),
                                               links[k]) != links.begin() + static_cast<std::ptrdiff_t>(k))
                    throw LoadError("a sector linked to itself, or twice to another");
                if (!map.linked(links[k], i))
                    throw LoadError("a sector link only one way");
            }
        }
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            for (const int k : map.at(i).neighbors)
                if (!reached[static_cast<std::size_t>(k)]) {
                    reached[static_cast<std::size_t>(k)] = true;
                    stack.push_back(k);
                }
        }
        if (std::find(reached.begin(), reached.end(), false) != reached.end())
            throw LoadError("a sector out of reach of home");
        return map;
    }

    static Json save_world(const World& world) {
        return {{"tick", world.current_tick}, {"star_map", save_map(world.map)}, {"fleet", save_fleet(world.fleet)}};
    }

    static World load_world(const Json& j) {
        World world;
        world.current_tick = load_tick(j.at("tick"), std::numeric_limits<Tick>::max() - kTransferTicks);
        world.map = load_map(j.at("star_map"));
        world.fleet = load_fleet(j.at("fleet"), world.current_tick, world.map);
        world.update_yields();
        return world;
    }
};

// ---------------------------------------------------------------------------
// The file
// ---------------------------------------------------------------------------

namespace {

SaveHeader load_header(const Json& j) {
    if (!j.is_object())
        throw LoadError("not an Ore in Orbit save");
    const auto format = j.find("format");
    if (format == j.end() || *format != kFormat)
        throw LoadError("not an Ore in Orbit save");
    const Json& version = j.at("version");
    if (version != kSaveVersion)
        throw LoadError("saved by another version of the game (format " + version.dump() + ")");
    SaveHeader header;
    header.name = load_string(j.at("name"), "a name");
    header.saved_at = load_int(j.at("saved_at"), std::numeric_limits<std::int64_t>::min(),
                               std::numeric_limits<std::int64_t>::max());
    header.played = load_tick(j.at("played"));
    return header;
}

}  // namespace

void write_save(std::ostream& out, const std::string& name, std::int64_t saved_at, const World& world) {
    const Json j = {
        {"format", kFormat},
        {"version", kSaveVersion},
        {"name", name},
        {"saved_at", saved_at},
        {"played", world.get_tick()},
        {"world", SaveAccess::save_world(world)},
    };
    // A name that isn't valid UTF-8 is written with replacement characters
    // rather than failing.
    out << j.dump(2, ' ', false, Json::error_handler_t::replace) << '\n';
}

std::optional<SaveHeader> read_save_header(std::istream& in, std::string& error) {
    try {
        return load_header(Json::parse(in));
    } catch (const LoadError& e) {
        error = e.what();
    } catch (const nlohmann::json::exception& e) {
        error = e.what();
    }
    return std::nullopt;
}

std::optional<LoadedSave> read_save(std::istream& in, std::string& error) {
    try {
        const Json j = Json::parse(in);
        SaveHeader header = load_header(j);
        return LoadedSave{std::move(header), SaveAccess::load_world(j.at("world"))};
    } catch (const LoadError& e) {
        error = e.what();
    } catch (const nlohmann::json::exception& e) {
        error = e.what();
    }
    return std::nullopt;
}
