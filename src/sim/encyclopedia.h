// encyclopedia.h — what the in-game encyclopedia says about each item, and
// the lookups behind its supply chains: what makes an item, what it goes
// into, what gathers it, fires it or carries it, and which deliveries and
// ship layouts want it. The tables these read (recipes.h, deliveries.h,
// building.h, ship_layout.h, enemies.h) stay the one source of truth: this
// only searches them.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
#pragma once

#include "sim/enemies.h"
#include "sim/game_types.h"
#include "sim/recipes.h"

#include <cstddef>
#include <vector>

// A paragraph about the item: what it is, and what it's for.
const char* item_description(ItemKind kind);

// The recipes with `kind` among their outputs, main output first.
std::vector<const Recipe*> recipes_making(ItemKind kind);
// The recipes taking `kind` in.
std::vector<const Recipe*> recipes_using(ItemKind kind);
// The machines that run recipes of `category` (the bridge, which
// hand-crafts, isn't one).
std::vector<ItemKind> machines_for(CraftCategory category);
// The collectors that can be aimed at `kind` (a raw resource).
std::vector<ItemKind> collectors_gathering(ItemKind kind);
// The weapons that fire `kind` (their ammunition).
std::vector<ItemKind> weapons_firing(ItemKind kind);
// The enemy classes that carry weapon `kind`, mounted or in their hold.
std::vector<EnemyClass> enemies_carrying(ItemKind kind);
// The deliveries asking for `kind`, in table order.
std::vector<std::size_t> deliveries_wanting(ItemKind kind);
// The ship layouts (indices into all_layouts()) whose cost takes `kind`;
// every one, for ship starter packs.
std::vector<std::size_t> layouts_costing(ItemKind kind);
// The techs that come straight after tech `t`.
std::vector<std::size_t> techs_after(std::size_t t);
