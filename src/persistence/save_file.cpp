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
// (ranges, known names, bounds, hull under buildings, overlaps, the bridge
// and engines where the ship's layout has them).
// A problem throws LoadError, or nlohmann's json::exception for a missing
// key or a value of the wrong type; read_save() turns either into `error`.
// A bad save never asserts.
#include "persistence/save_file.h"

#include "persistence/json_io.h"
#include "sim/building.h"
#include "sim/combat.h"
#include "sim/fleet.h"
#include "sim/milestones.h"
#include "sim/recipes.h"
#include "sim/sector.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"
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
// The longest a saved weapon or shield projector may be stunned for:
// longer than any weapon stuns it.
constexpr Tick kMaxStun = 60 * kTicksPerSecond;
// The furthest off a saved shot may land: longer than any flies.
constexpr Tick kMaxFlight = 10 * kTicksPerSecond;

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
        if (const std::optional<ItemKind> kind = load_item(Json(id)))
            inv.add(*kind, static_cast<int>(load_int(count, 0, kMaxSavedCount)));
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
            // Item requests, in the order they were asked for; only when
            // there are any.
            if (!bridge->requests.empty()) {
                Json requests = Json::array();
                for (const ItemStack& r : bridge->requests)
                    requests.push_back({{"item", item_id(r.kind)}, {"count", r.count}});
                j["requests"] = std::move(requests);
            }
        } else if (const auto* c = dynamic_cast<const Collector*>(&b)) {
            j["progress"] = c->progress;
            j["target"] = item_id(c->target);
            j["caught"] = save_inventory(c->caught);
            // Running only on request, only when it does.
            if (c->only_when_requested)
                j["only_when_requested"] = true;
        } else if (const auto* belt = dynamic_cast<const Conveyor*>(&b)) {
            Json items = Json::array();
            for (const BeltItem& item : belt->get_items()) {
                Json saved = {{"item", item_id(item.kind)}, {"pos", item.pos}};
                // Which side it came on by, for drawing it on its way in
                // (see BeltSide); only for the sides.
                if (item.from != BeltSide::Back)
                    saved["from"] = item.from == BeltSide::Left ? "left" : "right";
                items.push_back(std::move(saved));
            }
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
        } else if (const auto* unloader = dynamic_cast<const Unloader*>(&b)) {
            j["filter"] = save_optional_item(unloader->filter);
            j["cooldown"] = unloader->cooldown;
            j["next_kind"] = unloader->next_kind;
        } else if (const auto* router = dynamic_cast<const Router*>(&b)) {
            j["held"] = save_optional_item(router->held);
            j["came_from"] = direction_id(router->came_from);
            j["elapsed"] = router->elapsed;
            j["next_out"] = direction_id(router->next_out);
        } else if (const auto* hold = dynamic_cast<const CargoHold*>(&b)) {
            j["stored"] = save_inventory(hold->stored);
        } else if (const auto* thruster = dynamic_cast<const Thruster*>(&b)) {
            j["fuel"] = thruster->fuel;
        } else if (const auto* weapon = dynamic_cast<const Weapon*>(&b)) {
            j["fuel"] = weapon->fuel;
            j["ammo"] = weapon->ammo;
            j["charge"] = weapon->charge;
            j["stun"] = weapon->stun;
        } else if (const auto* shield = dynamic_cast<const ShieldProjector*>(&b)) {
            j["recharge"] = shield->recharge;
            j["stun"] = shield->stun;
        } else if (const auto* m = dynamic_cast<const Machine*>(&b)) {
            j["recipe"] = save_recipe(m->recipe);
            // How far along the craft under way is, or null when idle.
            j["work"] = m->work ? Json(*m->work) : Json(nullptr);
            j["input"] = save_inventory(m->input);
            j["output"] = save_inventory(m->output);
            if (m->only_when_requested)
                j["only_when_requested"] = true;
        }
        // Survey scanners hold no state.
    }

    // The fleet's milestone progress: the milestone being worked on, and
    // what has been delivered toward it, net, as item counts (below 0 when
    // more were unloaded than delivered).
    static Json save_progress(const Progress& progress) {
        Json delivered = Json::object();
        for (int i = 0; i < kItemKindCount; ++i)
            if (const int n = progress.delivered[static_cast<std::size_t>(i)]; n != 0)
                delivered[item_id(static_cast<ItemKind>(i))] = n;
        Json j = {{"milestone", progress.current}, {"delivered", std::move(delivered)}};
        // The kinds of weapon found.
        Json found = Json::array();
        for (const ItemKind w : progress.found)
            found.push_back(item_id(w));
        j["found_weapons"] = std::move(found);
        return j;
    }

    static Progress load_progress_of(const Json& j) {
        Progress progress;
        progress.current = static_cast<std::size_t>(
            load_int(j.at("milestone"), 0, static_cast<std::int64_t>(all_milestones().size())));
        const Json& delivered = j.at("delivered");
        if (!delivered.is_object())
            throw LoadError("expected an object of item counts, not " + delivered.dump());
        const Milestone* milestone = progress.current_milestone();
        for (const auto& [id, count] : delivered.items()) {
            const std::optional<ItemKind> known = load_item(Json(id));
            if (!known)
                continue;
            const ItemKind kind = *known;
            const bool asked = milestone && std::any_of(milestone->deliver.begin(), milestone->deliver.end(),
                                                        [&](const ItemStack& d) { return d.kind == kind; });
            if (!asked)
                throw LoadError(std::string(item_id(kind)) + " delivered, which the milestone doesn't ask for");
            progress.delivered[static_cast<std::size_t>(kind)] =
                static_cast<int>(load_int(count, -kMaxSavedCount, kMaxSavedCount));
        }
        // A milestone completes as soon as every count reaches what it
        // needs, so they can't all have.
        if (milestone && std::all_of(milestone->deliver.begin(), milestone->deliver.end(), [&](const ItemStack& d) {
                return progress.delivered[static_cast<std::size_t>(d.kind)] >= d.count;
            }))
            throw LoadError("everything the milestone needs delivered, but not complete");
        // The weapons found (none, without the list).
        if (const auto found = j.find("found_weapons"); found != j.end())
            for (const Json& w : load_array(*found, "found weapons")) {
                const std::optional<ItemKind> weapon = load_item(w);
                if (!weapon)
                    continue;  // an item no longer known
                if (!is_weapon(*weapon))
                    throw LoadError(std::string(item_id(*weapon)) + " found as a weapon, which it isn't");
                if (!progress.add_found(*weapon))
                    throw LoadError(std::string(item_id(*weapon)) + " found twice");
            }
        return progress;
    }

    // Whether a machine or collector runs only on request: not, in saves
    // from before it could.
    static bool load_on_request(const Json& j) {
        const auto on = j.find("only_when_requested");
        return on != j.end() && on->get<bool>();
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
                // A craft of an unknown recipe is dropped, its ingredients
                // (taken when it was queued) with it.
                const Recipe* recipe = load_recipe(job.at("recipe"));
                if (!recipe)
                    continue;
                if (!recipe->hand_craftable || !progress.unlocked(*recipe))
                    throw LoadError("the bridge can't craft " + job.at("recipe").dump());
                // A job finishes (and leaves the queue) when it reaches the
                // recipe's ticks.
                bridge->queue.push_back({recipe, load_progress(job.at("elapsed"), recipe->ticks)});
            }
            // None in saves from before there were requests.
            const auto requests = j.find("requests");
            for (const Json& r : requests == j.end() ? Json::array() : load_array(*requests, "requests")) {
                const std::optional<ItemKind> requested = load_item(r.at("item"));
                if (!requested)
                    continue;
                const ItemKind kind = *requested;
                if (is_layout_part(kind))
                    throw LoadError(std::string("a request for ") + item_id(kind) + ", which only comes with a layout");
                if (bridge->request_for(kind))
                    throw LoadError(std::string("two requests for ") + item_id(kind));
                bridge->requests.push_back({kind, static_cast<int>(load_int(r.at("count"), 0, kMaxRequest))});
            }
        } else if (auto* c = dynamic_cast<Collector*>(&b)) {
            // A catch takes progress back down when it reaches the
            // interval (in hundredths of a tick).
            c->progress = load_progress(j.at("progress"), c->spec->interval * 100);
            if (const std::optional<ItemKind> target = load_item(j.at("target"))) {
                if (!c->can_target(*target))
                    throw LoadError(std::string(item_id(*c->item_kind())) + " can't gather " + item_id(*target));
                c->target = *target;
            }
            c->caught = load_inventory(j.at("caught"));
            c->only_when_requested = load_on_request(j);
        } else if (auto* belt = dynamic_cast<Conveyor*>(&b)) {
            for (const Json& item : load_array(j.at("items"), "belt items")) {
                const std::optional<ItemKind> kind = load_item(item.at("item"));
                if (!kind)
                    continue;  // gone, leaving a gap
                // A tunnel's items may be on their way underground.
                const int furthest = dynamic_cast<const Tunnel*>(belt) ? kBeltLength * kTunnelRange : kBeltLength;
                const int pos = static_cast<int>(load_int(item.at("pos"), 0, furthest));
                if (!belt->has_room_at(pos))
                    throw LoadError("belt items too close together");
                // Older saves (and items in at the back) have no side.
                BeltSide from = BeltSide::Back;
                if (item.contains("from")) {
                    const std::string side = load_string(item.at("from"), "a belt side");
                    if (side != "left" && side != "right")
                        throw LoadError("unknown belt side " + side);
                    from = side == "left" ? BeltSide::Left : BeltSide::Right;
                }
                belt->insert(*kind, pos, from);
            }
        } else if (auto* sorter = dynamic_cast<Sorter*>(&b)) {
            sorter->set_filter(load_item(j.at("filter")));
            sorter->next_right = j.at("next_right").get<bool>();
        } else if (auto* junction = dynamic_cast<Junction*>(&b)) {
            const Json& lanes = j.at("lanes");
            for (int d = 0; d < 4; ++d) {
                const Json& lane = load_array(lanes.at(direction_id(static_cast<Direction>(d))), "junction items");
                if (lane.size() > kJunctionCapacity)
                    throw LoadError("too many items in a junction");
                for (const Json& item : lane)
                    if (const std::optional<ItemKind> kind = load_item(item.at("item")))
                        junction->lanes[static_cast<std::size_t>(d)].push_back(
                            {*kind, load_tick(item.at("elapsed"), kJunctionTicks)});
            }
        } else if (auto* unloader = dynamic_cast<Unloader*>(&b)) {
            unloader->filter = load_item(j.at("filter"));
            unloader->cooldown = load_tick(j.at("cooldown"), kUnloaderTicks);
            // Only where it starts looking next: a save from when there were
            // other items may point past the end, which wraps round.
            unloader->next_kind = static_cast<std::size_t>(load_int(j.at("next_kind"), 0, INT32_MAX)) %
                                  static_cast<std::size_t>(kItemKindCount);
        } else if (auto* router = dynamic_cast<Router*>(&b)) {
            router->held = load_item(j.at("held"));
            router->came_from = load_direction(j.at("came_from"));
            router->elapsed = load_tick(j.at("elapsed"), kRouterTicks);
            router->next_out = load_direction(j.at("next_out"));
        } else if (auto* hold = dynamic_cast<CargoHold*>(&b)) {
            hold->stored = load_inventory(j.at("stored"));
        } else if (auto* thruster = dynamic_cast<Thruster*>(&b)) {
            thruster->fuel = static_cast<int>(load_int(j.at("fuel"), 0, kThrusterCharge));
        } else if (auto* weapon = dynamic_cast<Weapon*>(&b)) {
            weapon->fuel = static_cast<int>(load_int(j.at("fuel"), 0, kWeaponFuelCapacity));
            weapon->ammo = static_cast<int>(load_int(j.at("ammo"), 0, kWeaponAmmoCapacity));
            weapon->charge =
                static_cast<int>(load_int(j.at("charge"), 0, static_cast<std::int64_t>(weapon->spec->charge) * kChargeScale));
            weapon->stun = load_tick(j.at("stun"), kMaxStun);
        } else if (auto* shield = dynamic_cast<ShieldProjector*>(&b)) {
            shield->recharge = load_tick(j.at("recharge"), kShieldRecharge);
            shield->stun = load_tick(j.at("stun"), kMaxStun);
        } else if (auto* m = dynamic_cast<Machine*>(&b)) {
            // An unknown recipe: none now, and a craft of it under way
            // dropped, its ingredients with it.
            const Recipe* recipe = load_recipe(j.at("recipe"));
            if (recipe && !m->can_run(*recipe))
                throw LoadError(std::string(item_id(*m->item_kind())) + " can't run " + recipe_name(*recipe));
            m->recipe = recipe;
            if (const Json& work = j.at("work"); recipe && !work.is_null()) {
                // A craft finishes (and work resets) when it reaches the
                // recipe's ticks.
                m->work = load_progress(work, recipe->ticks);
            }
            m->input = load_inventory(j.at("input"));
            m->output = load_inventory(j.at("output"));
            m->only_when_requested = load_on_request(j);
        }
    }

    // The layout (which the hull is), then every building in placement order
    // (which is also the order they tick in), its bridge and engines included.
    static Json save_ship(const Ship& ship) {
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
            // Health only when it isn't full.
            if (b.damaged())
                j["hp"] = b.hp;
            save_state(j, b);
            buildings.push_back(std::move(j));
        }
        return {{"layout", ship.layout->id}, {"buildings", std::move(buildings)}};
    }

    // Rebuilds the grids through Ship::add_building, so the anchor and
    // placement order come out as placing them would make them, checking the
    // placement rules a game can't break. Its bridge follows `progress`.
    static Ship load_ship(const Json& j, const std::shared_ptr<Progress>& progress) {
        Ship ship;
        const std::string id = load_string(j.at("layout"), "a ship layout");
        const ShipLayout* layout = find_layout(id);
        if (!layout)
            throw LoadError("unknown ship layout: " + id);
        ship.set_hull(*layout);
        for (const Json& b : load_array(j.at("buildings"), "buildings"))
            load_building(b, ship, progress);

        // The bridge and every engine where the layout has them, and no others.
        if (!ship.bridge || ship.building_anchored_at(layout_to_grid(*layout, layout->bridge)) != ship.bridge)
            throw LoadError("the ship has no bridge where its layout has it");
        for (const GridPos e : layout->engines)
            if (!dynamic_cast<const Thruster*>(ship.building_anchored_at(layout_to_grid(*layout, e))))
                throw LoadError("an engine missing from where the ship's layout has it");
        if (ship.thrusters().size() != layout->engines.size())
            throw LoadError("more engines than the ship's layout has");
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
            const std::optional<ItemKind> known = load_item(kind_json);
            if (!known)
                return;  // never there
            const ItemKind kind = *known;
            if (!is_building(kind))
                throw LoadError(std::string("not a building: ") + item_id(kind));
            b = make_building(kind, load_direction(j.at("facing")));
        }
        // push_output keeps it below the number of cells around the footprint.
        b->next_output_side = static_cast<std::size_t>(load_int(j.at("output_side"), 0, 1'000));
        // Only an engine stays at 0 (knocked offline): anything else is
        // destroyed, and the bridge's ship lost.
        if (const auto hp = j.find("hp"); hp != j.end())
            b->hp = static_cast<int>(load_int(*hp, dynamic_cast<const Thruster*>(b.get()) ? 0 : 1, b->max_hp()));

        const GridPos p{load_coord(j.at("x")), load_coord(j.at("y"))};
        const GridSize size = b->footprint();
        if (!ship.in_bounds({p.x + size.w - 1, p.y + size.h - 1}))
            throw LoadError("a building sticks out of the grid");
        for (int y = p.y; y < p.y + size.h; ++y)
            for (int x = p.x; x < p.x + size.w; ++x) {
                if (!ship.has_hull({x, y}))
                    throw LoadError("a building without the hull it needs under it");
                if (ship.anchor_of({x, y}))
                    throw LoadError("buildings overlap");
            }

        if (const std::optional<ItemKind> kind = b->item_kind())
            if (const BuildResult ports = ship.check_ports(*kind, p, size); ports != BuildResult::Ok)
                throw LoadError(std::string("a ") + item_id(*kind) + " not where the ports allow: " + describe(ports));

        load_state(j, *b);
        if (auto* bridge = dynamic_cast<Bridge*>(b.get()))
            ship.bridge = bridge;
        ship.add_building(p, std::move(b));
    }

    // A fight: the sector, the enemy's class and ship, and the shots on
    // their way.
    static Json save_engagement(const Engagement& e) {
        Json shots = Json::array();
        for (const Shot& shot : e.shots)
            shots.push_back({
                {"at_enemy", shot.at_enemy},
                {"ship", shot.ship},
                {"weapon", item_id(shot.weapon)},
                {"x", shot.cell.x},
                {"y", shot.cell.y},
                {"sweep", direction_id(shot.sweep)},
                {"fired", shot.fired},
                {"lands", shot.lands},
            });
        return {{"sector", e.sector},
                {"enemy", enemy_id(e.enemy)},
                {"ship", save_ship(e.ship)},
                {"shots", std::move(shots)}};
    }

    // `now`: the World's tick, which shots land after; the fight is in one
    // of `map`'s sectors that has its enemy waiting, and some of `fleet`'s
    // ships are there.
    static Engagement load_engagement(const Json& j, Tick now, const StarMap& map, const Fleet& fleet) {
        Engagement e;
        e.sector = static_cast<int>(load_int(j.at("sector"), 0, map.size() - 1));
        const std::optional<EnemyClass> enemy = enemy_from_id(load_string(j.at("enemy"), "an enemy class"));
        if (!enemy)
            throw LoadError("an unknown enemy: " + j.at("enemy").dump());
        e.enemy = *enemy;
        if (map.at(e.sector).enemy != e.enemy)
            throw LoadError("a fight with an enemy that isn't in its sector");
        if (std::none_of(fleet.ships.begin(), fleet.ships.end(),
                         [&](const std::unique_ptr<FleetShip>& s) { return s->sector == e.sector; }))
            throw LoadError("a fight with none of the fleet's ships there");
        // The enemy's bridge follows a progress of its own: it crafts nothing.
        e.ship = load_ship(j.at("ship"), std::make_shared<Progress>());
        // Its health is saved only when hurt, so a full one reads as any
        // bridge's full health.
        if (const Bridge& b = e.ship.get_bridge(); b.damaged() && b.get_hp() > e.design().hull)
            throw LoadError("an enemy's bridge tougher than its class");
        e.ship.get_bridge().set_max_hp(e.design().hull);
        if (e.ship.get_layout()->id != std::string_view(e.design().layout))
            throw LoadError("an enemy ship in another class's layout");
        if (e.ship.bridge_destroyed())
            throw LoadError("an enemy beaten but still fighting");
        const Json& shots = load_array(j.at("shots"), "shots");
        if (shots.size() > 10'000)
            throw LoadError("too many shots");
        for (const Json& js : shots) {
            Shot shot;
            shot.at_enemy = js.at("at_enemy").get<bool>();
            shot.ship = static_cast<int>(load_int(js.at("ship"), 1, fleet.next_id - 1));
            const std::optional<ItemKind> weapon = load_item(js.at("weapon"));
            if (!weapon)
                continue;  // a weapon no longer known: its shot is lost
            if (!is_weapon(*weapon))
                throw LoadError(std::string("a shot from ") + item_id(*weapon) + ", which isn't a weapon");
            shot.weapon = *weapon;
            shot.cell = {load_coord(js.at("x")), load_coord(js.at("y"))};
            shot.sweep = load_direction(js.at("sweep"));
            shot.lands = load_tick(js.at("lands"), now + kMaxFlight);
            if (shot.lands <= now)
                throw LoadError("a shot that should have landed already");
            shot.fired = load_tick(js.at("fired"), shot.lands);
            e.shots.push_back(shot);
        }
        return e;
    }

    // The ships, oldest first, and the cargo in flight between them.
    static Json save_fleet(const Fleet& fleet) {
        Json ships = Json::array();
        for (const std::unique_ptr<FleetShip>& s : fleet.ships) {
            Json ship = {{"id", s->id},
                         {"name", s->name},
                         {"sector", s->sector},
                         {"ship", save_ship(s->ship)}};
            // Only while it has a target.
            if (s->target)
                ship["target"] = {{"x", s->target->x}, {"y", s->target->y}};
            // Only for a provider.
            if (s->provides)
                ship["provides"] = true;
            ships.push_back(std::move(ship));
        }
        Json transfers = Json::array();
        for (const Transfer& t : fleet.transfers) {
            Inventory items;
            items.add(t.items);
            Json transfer = {{"from", t.from}, {"to", t.to}, {"items", save_inventory(items)}, {"arrives", t.arrives}};
            // Only for cargo a provider sent.
            if (t.automatic)
                transfer["automatic"] = true;
            transfers.push_back(std::move(transfer));
        }
        Json j = {
            {"next_ship_id", fleet.next_id},
            {"progress", save_progress(*fleet.progress)},
            {"ships", std::move(ships)},
            {"transfers", std::move(transfers)},
        };
        // Part-filled pods providers are holding back, and since when; only
        // when there are some.
        if (!fleet.partial_since.empty()) {
            Json waiting = Json::array();
            for (const auto& [pair, since] : fleet.partial_since)
                waiting.push_back({{"from", pair.first}, {"to", pair.second}, {"since", since}});
            j["waiting"] = std::move(waiting);
        }
        return j;
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
            fleet.ships.push_back(std::make_unique<FleetShip>(FleetShip{
                id, std::move(name), load_ship(s.at("ship"), fleet.progress), sector, std::nullopt}));
            if (const auto target = s.find("target"); target != s.end())
                fleet.ships.back()->target = GridPos{load_coord(target->at("x")), load_coord(target->at("y"))};
            // Not a provider in saves from before there were providers.
            if (const auto provides = s.find("provides"); provides != s.end())
                fleet.ships.back()->provides = provides->get<bool>();
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
            if (const auto automatic = t.find("automatic"); automatic != t.end())
                transfer.automatic = automatic->get<bool>();
            fleet.transfers.push_back(std::move(transfer));
        }
        // None in saves from before there were providers.
        if (const auto waiting = j.find("waiting"); waiting != j.end())
            for (const Json& w : load_array(*waiting, "waiting")) {
                const int from = static_cast<int>(load_int(w.at("from"), 1, fleet.next_id - 1));
                const int to = static_cast<int>(load_int(w.at("to"), 1, fleet.next_id - 1));
                const Tick since = load_tick(w.at("since"), now);
                fleet.partial_since.push_back({{from, to}, since});
            }
        return fleet;
    }

    // Every sector, home first: where it is on the map, its links (by
    // index), its yields and what's left to gather (by raw resource), its
    // threat, and what the player knows of it.
    static Json save_map(const StarMap& map) {
        Json sectors = Json::array();
        for (const Sector& s : map.sectors) {
            Json yields = Json::object();
            Json reserves = Json::object();
            for (int r = 0; r < kRawResourceCount; ++r) {
                yields[item_id(kRawResources[r])] = s.yields[static_cast<std::size_t>(r)];
                reserves[item_id(kRawResources[r])] = s.reserves[static_cast<std::size_t>(r)];
            }
            sectors.push_back({
                {"name", s.name},
                {"x", s.x},
                {"y", s.y},
                {"neighbors", s.neighbors},
                {"yields", std::move(yields)},
                {"reserves", std::move(reserves)},
                {"threat", s.threat},
                {"enemy", s.enemy ? Json(enemy_id(*s.enemy)) : Json(nullptr)},
                {"beaten", s.beaten},
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
            // Saves from before resources ran out: as much as a new map has.
            if (const auto reserves = js.find("reserves"); reserves != js.end()) {
                for (int r = 0; r < kRawResourceCount; ++r)
                    s.reserves[static_cast<std::size_t>(r)] =
                        static_cast<int>(load_int(reserves->at(item_id(kRawResources[r])), 0, 100'000'000));
                if (reserves->size() != static_cast<std::size_t>(kRawResourceCount))
                    throw LoadError("reserves of something that isn't a raw resource");
            } else {
                s.reserves = starting_reserves(s, map.sectors.empty());
            }
            s.threat = static_cast<int>(load_int(js.at("threat"), 0, kMaxThreat));
            if (const Json& enemy = js.at("enemy"); !enemy.is_null()) {
                s.enemy = enemy_from_id(load_string(enemy, "an enemy class"));
                if (!s.enemy)
                    throw LoadError("an unknown enemy: " + enemy.dump());
            }
            s.beaten = js.at("beaten").get<bool>();
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
        return {{"tick", world.current_tick},
                {"rng", world.rng.state},
                {"star_map", save_map(world.map)},
                {"fleet", save_fleet(world.fleet)},
                {"engagements", [&] {
                     Json fights = Json::array();
                     for (const Engagement& e : world.engagements)
                         fights.push_back(save_engagement(e));
                     return fights;
                 }()}};
    }

    static World load_world(const Json& j) {
        World world;
        // Far enough from the end that adding a delay to it can't overflow.
        world.current_tick = load_tick(j.at("tick"), std::numeric_limits<Tick>::max() / 2);
        if (!j.at("rng").is_number_unsigned())
            throw LoadError("expected the random number state, not " + j.at("rng").dump());
        world.rng.state = j.at("rng").get<std::uint64_t>();
        world.map = load_map(j.at("star_map"));
        world.fleet = load_fleet(j.at("fleet"), world.current_tick, world.map);
        for (const Json& e : load_array(j.at("engagements"), "fights")) {
            Engagement fight = load_engagement(e, world.current_tick, world.map, world.fleet);
            if (world.engagement_in(fight.sector))
                throw LoadError("two fights in one sector");
            world.engagements.push_back(std::move(fight));
        }
        // Targets on the enemy there, if there's a fight (else none).
        for (const std::unique_ptr<FleetShip>& s : world.fleet.ships)
            if (s->target) {
                const Engagement* fight = world.engagement_in(s->sector);
                if (!fight || !fight->ship.building_anchored_at(*s->target))
                    throw LoadError("a ship targeting nothing there");
            }
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
