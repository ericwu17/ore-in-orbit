// json_io.cpp — JSON helpers shared by saves and blueprints (see json_io.h).
#include "persistence/json_io.h"

namespace json_io {

const char* direction_id(Direction d) {
    switch (d) {
    case Direction::North: return "North";
    case Direction::East:  return "East";
    case Direction::South: return "South";
    case Direction::West:  return "West";
    }
    return "?";
}

std::string load_string(const Json& j, const char* what) {
    if (!j.is_string())
        throw LoadError(std::string("expected ") + what + ", not " + j.dump());
    return j.get<std::string>();
}

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

const Recipe* load_recipe(const Json& j) {
    if (j.is_null())
        return nullptr;
    const std::string name = load_string(j, "a recipe");
    for (const Recipe& r : all_recipes())
        if (name == recipe_name(r))
            return &r;
    throw LoadError("unknown recipe \"" + name + "\"");
}

const Json& load_array(const Json& j, const char* what) {
    if (!j.is_array())
        throw LoadError(std::string("expected a list of ") + what + ", not " + j.dump());
    return j;
}

}  // namespace json_io
