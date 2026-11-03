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

// Items and recipes come and go between versions of the game, and saves and
// blueprints name them without being of an older format. Loading leaves out
// any it doesn't recognize, and whatever names one (a building, an item on a
// belt, a craft), as if they had never been, instead of refusing the file.

// An item, or null for none.
Json save_optional_item(std::optional<ItemKind> kind);
// An item, or nullopt for null or one the game doesn't know.
std::optional<ItemKind> load_item(const Json& j);

Direction load_direction(const Json& j);

// A recipe by name, or null for none (or one the game doesn't know).
Json save_recipe(const Recipe* recipe);
const Recipe* load_recipe(const Json& j);

// `j`, which must be an array (of `what`, for the error).
const Json& load_array(const Json& j, const char* what);

}  // namespace json_io
