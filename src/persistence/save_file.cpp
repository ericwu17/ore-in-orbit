// save_file.cpp — the save format: the World as JSON (vendored
// nlohmann::json, third_party/nlohmann/), and reading it back (see
// save_file.h).
//
// Everything about saving lives here, not in the simulation: SaveAccess, at
// the bottom, is the one struct the simulation classes open their private
// state to (their `friend struct SaveAccess`).
//
// Items and directions are written by their enumerator's name and recipes
// by recipe_name, never by number, so reordering an enum or the recipe table doesn't break old saves. (The makefile sets
// JSON_DISABLE_ENUM_SERIALIZATION, so an enum can't be written as a number
// by mistake.)
//
// Reading checks everything that could break the simulation's invariants
// (ranges, known names, bounds, hull under buildings, overlaps, one bridge).
// A problem throws LoadError, or nlohmann's json::exception for a missing
// key or a value of the wrong type; read_save() turns either into `error`.
// A bad save never asserts.
#include "persistence/save_file.h"

#include "sim/building.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/world.h"

#include "nlohmann/json.hpp"

#include <istream>
#include <limits>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// Objects keep their keys in the order written, so saves read naturally
// (kind and position first).
using Json = nlohmann::ordered_json;

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

class LoadError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

// Items are written by item_id (sim/game_types.h).

const char* direction_id(Direction d) {
    switch (d) {
    case Direction::North: return "North";
    case Direction::East:  return "East";
    case Direction::South: return "South";
    case Direction::West:  return "West";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Checked reading
// ---------------------------------------------------------------------------

std::string load_string(const Json& j, const char* what) {
    if (!j.is_string())
        throw LoadError(std::string("expected ") + what + ", not " + j.dump());
    return j.get<std::string>();
}

// An integer from lo to hi (json's own get<int>() doesn't check the range).
std::int64_t load_int(const Json& j, std::int64_t lo, std::int64_t hi) {
    // Non-negative numbers parse as unsigned; compare those unsigned, so a
    // huge one isn't wrapped into range.
    const bool in_range = j.is_number_unsigned()
                              ? hi >= 0 && j.get<std::uint64_t>() <= static_cast<std::uint64_t>(hi) &&
                                    static_cast<std::int64_t>(j.get<std::uint64_t>()) >= lo
                              : j.is_number_integer() && j.get<std::int64_t>() >= lo && j.get<std::int64_t>() <= hi;
    if (!in_range)
        throw LoadError("expected a whole number from " + std::to_string(lo) + " to " + std::to_string(hi) +
                        ", not " + j.dump());
    return j.get<std::int64_t>();
}

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

std::optional<ItemKind> item_from_id(std::string_view id) {
    for (int i = 0; i < kItemKindCount; ++i)
        if (id == item_id(static_cast<ItemKind>(i)))
            return static_cast<ItemKind>(i);
    return std::nullopt;
}

ItemKind load_item(const Json& j) {
    const std::string id = load_string(j, "an item");
    const std::optional<ItemKind> kind = item_from_id(id);
    if (!kind)
        throw LoadError("unknown item \"" + id + "\"");
    return *kind;
}

// An item, or null for none.
Json save_optional_item(std::optional<ItemKind> kind) {
    return kind ? Json(item_id(*kind)) : Json(nullptr);
}

std::optional<ItemKind> load_optional_item(const Json& j) {
    return j.is_null() ? std::nullopt : std::optional<ItemKind>(load_item(j));
}

Direction load_direction(const Json& j) {
    const std::string id = load_string(j, "a direction");
    for (int i = 0; i < 4; ++i)
        if (id == direction_id(static_cast<Direction>(i)))
            return static_cast<Direction>(i);
    throw LoadError("unknown direction \"" + id + "\"");
}

Json save_recipe(const Recipe* recipe) {
    return recipe ? Json(recipe_name(*recipe)) : Json(nullptr);
}

// A recipe by name, or null for none.
const Recipe* load_recipe(const Json& j) {
    if (j.is_null())
        return nullptr;
    const std::string name = load_string(j, "a recipe");
    for (const Recipe& r : all_recipes())
        if (name == recipe_name(r))
            return &r;
    throw LoadError("unknown recipe \"" + name + "\"");
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

const Json& load_array(const Json& j, const char* what) {
    if (!j.is_array())
        throw LoadError(std::string("expected a list of ") + what + ", not " + j.dump());
    return j;
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
            j["chunk_turn"] = c->chunk_turn;
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
        } else if (const auto* m = dynamic_cast<const Machine*>(&b)) {
            j["recipe"] = save_recipe(m->recipe);
            // How far along the craft under way is, or null when idle.
            j["work"] = m->work ? Json(*m->work) : Json(nullptr);
            j["input"] = save_inventory(m->input);
            j["output"] = save_inventory(m->output);
        }
        // Power and ship systems hold no state yet.
    }

    // Reads a building's own state from `j` into the new building `b`.
    static void load_state(const Json& j, Building& b) {
        if (auto* bridge = dynamic_cast<Bridge*>(&b)) {
            bridge->inventory = load_inventory(j.at("inventory"));
            const Json& jobs = load_array(j.at("queue"), "crafts");
            if (jobs.size() > kMaxSavedQueue)
                throw LoadError("too many crafts queued");
            for (const Json& job : jobs) {
                const Recipe* recipe = load_recipe(job.at("recipe"));
                if (!recipe || !recipe->hand_craftable)
                    throw LoadError("the bridge can't craft " + job.at("recipe").dump());
                // A job finishes (and leaves the queue) when it reaches the
                // recipe's ticks.
                bridge->queue.push_back({recipe, load_progress(job.at("elapsed"), recipe->ticks)});
            }
        } else if (auto* c = dynamic_cast<Collector*>(&b)) {
            // Progress goes back to 0 when it reaches the interval.
            c->progress = load_progress(j.at("progress"), c->spec->interval);
            const Json& turns = load_array(j.at("chunk_turn"), "chunk turns");
            if (turns.size() != c->chunk_turn.size())
                throw LoadError("expected " + std::to_string(c->chunk_turn.size()) + " chunk turns");
            for (std::size_t i = 0; i < c->chunk_turn.size(); ++i)
                c->chunk_turn[i] = static_cast<int>(load_int(turns[i], -1'000'000, 1'000'000));
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
    // placement rules a game can't break.
    static Ship load_ship(const Json& j) {
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
            load_building(b, ship);
        if (!ship.bridge)
            throw LoadError("the ship has no bridge");
        return ship;
    }

    static void load_building(const Json& j, Ship& ship) {
        const Json& kind_json = j.at("kind");
        std::unique_ptr<Building> b;
        if (kind_json == kBridgeId) {
            if (ship.bridge)
                throw LoadError("more than one bridge");
            b = std::make_unique<Bridge>();
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

    static Json save_world(const World& world) {
        return {{"tick", world.current_tick}, {"ship", save_ship(world.ship)}};
    }

    static World load_world(const Json& j) {
        World world;
        world.current_tick = load_tick(j.at("tick"));
        world.ship = load_ship(j.at("ship"));
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
