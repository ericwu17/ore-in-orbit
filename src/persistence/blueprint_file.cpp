// blueprint_file.cpp — blueprint strings and the library folder (see
// blueprint_file.h).
#include "persistence/blueprint_file.h"

#include "persistence/files.h"
#include "persistence/json_io.h"
#include "sim/building.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <memory>
#include <ostream>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;
using namespace json_io;

namespace {

constexpr const char* kFormat = "ore-in-orbit-blueprint";
constexpr const char* kExtension = ".json";

char hull_char(std::optional<ItemKind> tile) {
    if (!tile)
        return '.';
    return *tile == ItemKind::ReinforcedHull ? 'R' : 'H';
}

std::optional<ItemKind> load_hull_char(char c) {
    switch (c) {
    case '.': return std::nullopt;
    case 'H': return ItemKind::Hull;
    case 'R': return ItemKind::ReinforcedHull;
    default:
        throw LoadError(std::string("unknown hull tile '") + c + "' (expected . H or R)");
    }
}

Json save_blueprint(const Blueprint& blueprint) {
    Json rows = Json::array();
    for (int y = 0; y < blueprint.size.h; ++y) {
        std::string row;
        for (int x = 0; x < blueprint.size.w; ++x)
            row += hull_char(blueprint.hull_at({x, y}));
        rows.push_back(std::move(row));
    }
    Json buildings = Json::array();
    for (const BlueprintBuilding& b : blueprint.buildings) {
        Json j = {
            {"kind", item_id(b.kind)},
            {"x", b.offset.x},
            {"y", b.offset.y},
            {"facing", direction_id(b.facing)},
        };
        if (b.recipe)
            j["recipe"] = save_recipe(b.recipe);
        if (b.target)
            j["target"] = item_id(*b.target);
        if (b.filter)
            j["filter"] = item_id(*b.filter);
        buildings.push_back(std::move(j));
    }
    return {
        {"format", kFormat},
        {"version", kBlueprintVersion},
        {"name", blueprint.name},
        {"width", blueprint.size.w},
        {"height", blueprint.size.h},
        {"hull", std::move(rows)},
        {"buildings", std::move(buildings)},
    };
}

// Reads one building into `blueprint`, checking it fits inside it, clear of
// the ones before (`taken`, per cell), and that its settings suit it.
void load_building(const Json& j, Blueprint& blueprint, std::vector<bool>& taken) {
    const ItemKind kind = load_item(j.at("kind"));
    if (!is_placeable(kind) || kind == ItemKind::Hull || kind == ItemKind::ReinforcedHull)
        throw LoadError(std::string("not a building: ") + item_id(kind));
    BlueprintBuilding b;
    b.kind = kind;
    b.offset = {static_cast<int>(load_int(j.at("x"), 0, blueprint.size.w - 1)),
                static_cast<int>(load_int(j.at("y"), 0, blueprint.size.h - 1))};
    b.facing = load_direction(j.at("facing"));
    if (!is_rotatable(kind) && b.facing != Direction::North)
        throw LoadError(std::string(item_id(kind)) + " doesn't turn: it can only face North");

    const GridSize size = footprint_of(kind, b.facing);
    if (b.offset.x + size.w > blueprint.size.w || b.offset.y + size.h > blueprint.size.h)
        throw LoadError(std::string("a ") + item_id(kind) + " sticks out of the blueprint");
    for (int y = b.offset.y; y < b.offset.y + size.h; ++y)
        for (int x = b.offset.x; x < b.offset.x + size.w; ++x) {
            const std::size_t cell = static_cast<std::size_t>(y * blueprint.size.w + x);
            if (taken[cell])
                throw LoadError("buildings overlap");
            taken[cell] = true;
        }

    // The settings, checked against a building of that kind.
    const std::unique_ptr<Building> building = make_building(kind, b.facing);
    if (const auto recipe = j.find("recipe"); recipe != j.end()) {
        const auto* machine = dynamic_cast<const Machine*>(building.get());
        if (!machine)
            throw LoadError(std::string(item_id(kind)) + " has no recipe");
        b.recipe = load_recipe(*recipe);
        if (b.recipe && !machine->can_run(*b.recipe))
            throw LoadError(std::string(item_id(kind)) + " can't run " + recipe_name(*b.recipe));
    }
    if (const auto target = j.find("target"); target != j.end()) {
        const auto* collector = dynamic_cast<const Collector*>(building.get());
        if (!collector)
            throw LoadError(std::string(item_id(kind)) + " has no target");
        b.target = load_item(*target);
        if (!collector->can_target(*b.target))
            throw LoadError(std::string(item_id(kind)) + " can't gather " + item_id(*b.target));
    }
    if (const auto filter = j.find("filter"); filter != j.end()) {
        if (!dynamic_cast<const Sorter*>(building.get()))
            throw LoadError(std::string(item_id(kind)) + " has no filter");
        b.filter = load_optional_item(*filter);
    }
    blueprint.buildings.push_back(b);
}

Blueprint load_blueprint(const Json& j) {
    if (!j.is_object())
        throw LoadError("not an Ore in Orbit blueprint");
    const auto format = j.find("format");
    if (format == j.end() || *format != kFormat)
        throw LoadError("not an Ore in Orbit blueprint");
    const Json& version = j.at("version");
    if (version != kBlueprintVersion)
        throw LoadError("made by another version of the game (format " + version.dump() + ")");

    Blueprint blueprint;
    blueprint.name = load_string(j.at("name"), "a name");
    blueprint.size = {static_cast<int>(load_int(j.at("width"), 1, kShipGridSize)),
                      static_cast<int>(load_int(j.at("height"), 1, kShipGridSize))};
    const Json& rows = load_array(j.at("hull"), "hull rows");
    if (rows.size() != static_cast<std::size_t>(blueprint.size.h))
        throw LoadError("expected " + std::to_string(blueprint.size.h) + " hull rows, not " +
                        std::to_string(rows.size()));
    for (const Json& row_json : rows) {
        const std::string row = load_string(row_json, "a hull row");
        if (row.size() != static_cast<std::size_t>(blueprint.size.w))
            throw LoadError("expected hull rows " + std::to_string(blueprint.size.w) + " long, not " +
                            std::to_string(row.size()));
        for (const char c : row)
            blueprint.hull.push_back(load_hull_char(c));
    }
    std::vector<bool> taken(blueprint.hull.size());
    for (const Json& b : load_array(j.at("buildings"), "buildings"))
        load_building(b, blueprint, taken);
    return blueprint;
}

bool write_blueprint_file(const fs::path& path, const Blueprint& blueprint, std::string& error) {
    return write_file_atomically(
        path, [&](std::ostream& out) { out << blueprint_to_string(blueprint) << '\n'; }, error);
}

std::optional<Blueprint> read_blueprint_file(const fs::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "can't open " + path.filename().string();
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return blueprint_from_string(text, error);
}

// `s` in lower case, to sort names ignoring case.
std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

std::string blueprint_to_string(const Blueprint& blueprint) {
    // A name that isn't valid UTF-8 is written with replacement characters
    // rather than failing.
    return save_blueprint(blueprint).dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::optional<Blueprint> blueprint_from_string(std::string_view text, std::string& error) {
    try {
        return load_blueprint(Json::parse(text.begin(), text.end()));
    } catch (const LoadError& e) {
        error = e.what();
    } catch (const nlohmann::json::exception& e) {
        error = e.what();
    }
    return std::nullopt;
}

std::vector<BlueprintSlot> list_blueprints(const fs::path& dir) {
    std::vector<BlueprintSlot> slots;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != kExtension)
            continue;
        BlueprintSlot slot{it->path(), std::nullopt, {}};
        slot.blueprint = read_blueprint_file(slot.path, slot.error);
        slots.push_back(std::move(slot));
    }
    std::sort(slots.begin(), slots.end(), [](const BlueprintSlot& a, const BlueprintSlot& b) {
        if (a.blueprint.has_value() != b.blueprint.has_value())
            return a.blueprint.has_value();
        if (a.blueprint) {
            const std::string an = lower(a.blueprint->name);
            const std::string bn = lower(b.blueprint->name);
            if (an != bn)
                return an < bn;
        }
        return a.path.filename() < b.path.filename();
    });
    return slots;
}

std::optional<fs::path> add_blueprint(const fs::path& dir, const Blueprint& blueprint, std::string& error) {
    std::optional<fs::path> path = new_numbered_path(dir, "blueprint", kExtension, "blueprints", error);
    if (!path || !write_blueprint_file(*path, blueprint, error))
        return std::nullopt;
    return path;
}

bool rename_blueprint(const fs::path& path, const std::string& new_name, std::string& error) {
    std::optional<Blueprint> blueprint = read_blueprint_file(path, error);
    if (!blueprint)
        return false;
    blueprint->name = new_name;
    return write_blueprint_file(path, *blueprint, error);
}

bool delete_blueprint(const fs::path& path, std::string& error) {
    return delete_file(path, error);
}
