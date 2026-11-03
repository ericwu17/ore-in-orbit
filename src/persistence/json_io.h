// json_io.h — the pieces of reading and writing JSON that saves and
// blueprints share (vendored nlohmann::json, third_party/nlohmann/): items,
// directions and recipes by name, and checked reading that throws LoadError
// with a reason instead of trusting the file.
// Internal to the persistence layer; headless like the simulation.
//
// Items are written by item_id (sim/game_types.h), directions by their
// enumerator's name and recipes by recipe_name, never by number, so
// reordering an enum or the recipe table doesn't break what was written. (The
// makefile sets JSON_DISABLE_ENUM_SERIALIZATION, so an enum can't be written
// as a number by mistake.)
#pragma once

#include "sim/game_types.h"
#include "sim/recipes.h"

#include "nlohmann/json.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace json_io {

// Objects keep their keys in the order written, so files read naturally
// (kind and position first).
using Json = nlohmann::ordered_json;

// What reading throws for a value that is there but wrong (nlohmann's
// json::exception covers missing keys and wrong types).
class LoadError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

const char* direction_id(Direction d);

std::string load_string(const Json& j, const char* what);
// An integer from lo to hi (json's own get<int>() doesn't check the range).
std::int64_t load_int(const Json& j, std::int64_t lo, std::int64_t hi);

std::optional<ItemKind> item_from_id(std::string_view id);
ItemKind load_item(const Json& j);
// An item, or null for none.
Json save_optional_item(std::optional<ItemKind> kind);
// An item, or nullopt for null or a removed item (see is_removed_item).
std::optional<ItemKind> load_optional_item(const Json& j);

// Items the game once had and has since forgotten (the power buildings:
// solar panel, solar array, battery bank), by id. Older saves and blueprints
// still name them, without being an older format: loading leaves them out,
// as if they had never been, instead of refusing the file.
inline constexpr std::array<std::string_view, 3> kRemovedItems = {"SolarPanel", "SolarArray", "BatteryBank"};
// The recipes that made them, by name, left out likewise.
inline constexpr std::array<std::string_view, 3> kRemovedRecipes = {"Solar panel", "Solar array", "Battery bank"};
bool is_removed_item(std::string_view id);
// An item, or nullopt for a removed one (any other unknown item still
// throws).
std::optional<ItemKind> load_item_unless_removed(const Json& j);

Direction load_direction(const Json& j);

// A recipe by name, or null for none.
Json save_recipe(const Recipe* recipe);
const Recipe* load_recipe(const Json& j);
// A recipe (or null for none), or nullopt for a removed one (any other
// unknown recipe still throws).
std::optional<const Recipe*> load_recipe_unless_removed(const Json& j);

// `j`, which must be an array (of `what`, for the error).
const Json& load_array(const Json& j, const char* what);

}  // namespace json_io
