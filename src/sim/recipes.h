// recipes.h — every recipe in the game: what goes in, what comes out, how
// long it takes and which kind of building makes it. A building's cost is the
// recipe that makes its item.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// The production chain:
//   collectors gather one raw resource each: ores, silica, carbon
//   ore, silica, carbon --Smelting--> plates, glass, steel
//   carbon, silica, iridium ore --Refining--> plastic, silicon, iridium plate,
//                                             fuel cells
//   plates        --Assembly--> components --Assembly--> buildings
#pragma once

#include "sim/game_types.h"

#include <vector>

// What kind of building a recipe needs. Each machine handles one or more of
// these (see Machine in building.h); the bridge hand-crafts the recipes
// marked hand_craftable.
enum class CraftCategory {
    Smelting,      // Solar furnace, Refinery
    Refining,      // Refinery only
    Assembly,      // Fabricators
    Count,         // not a category: the number of categories
};

constexpr int kCraftCategoryCount = static_cast<int>(CraftCategory::Count);

const char* craft_category_name(CraftCategory category);

struct ItemStack {
    ItemKind kind;
    int count = 1;
};

struct Recipe {
    const char* name = nullptr;  // null: named after its first output (see recipe_name)
    CraftCategory category = CraftCategory::Assembly;
    Tick ticks = 0;              // crafting time at speed 1
    std::vector<ItemStack> inputs;
    std::vector<ItemStack> outputs;
    bool hand_craftable = false;  // the bridge can make it too
};

// The player-facing name of a recipe ("Gear", "Iron plate", ...).
const char* recipe_name(const Recipe& recipe);

// Every recipe, in a fixed order (by category, then roughly by tier).
const std::vector<Recipe>& all_recipes();

// The recipe whose main (first) output is `kind`, or null if none: raw
// resources are only ever gathered. For a placeable item, this is what
// building one costs.
const Recipe* recipe_for(ItemKind kind);
