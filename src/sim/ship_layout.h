// ship_layout.h — the ship layouts: the fixed shapes a ship is built in, each
// with its hull, its bridge and its ion thrusters in place, and what it costs.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// The player picks a layout when building a new ship (see Fleet::launch),
// paying one ship starter pack and the layout's cost; a new game's flagship
// is the first, smallest one (starter_layout). The hull never changes after:
// buildings go on it, and the bridge and engines stay where the layout puts
// them. Bigger layouts unlock with the milestones.
//
// A layout is drawn in its own coordinates, from its top-left corner (0, 0);
// a ship centers it on the build grid (layout_origin).
#pragma once

#include "sim/game_types.h"
#include "sim/milestones.h"
#include "sim/recipes.h"

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

// A rectangle of hull, in layout coordinates.
struct HullRect {
    GridPos at;
    GridSize size;
};

struct ShipLayout {
    const char* id;           // stable: saves name a ship's layout by it
    const char* name;         // player-facing ("Skiff")
    const char* description;  // a line for the build window
    std::vector<HullRect> hull;            // the hull is their union
    GridPos bridge;                        // the bridge's top-left cell
    std::vector<GridPos> engines;          // each ion thruster's top-left cell (facing North)
    std::vector<ItemStack> cost;           // on top of one ship starter pack
    std::optional<std::size_t> milestone;  // unlocked once this one is complete; nullopt: from the start
};

// Every layout, smallest first.
const std::vector<ShipLayout>& all_layouts();
// The layout a new game's flagship has: the smallest.
const ShipLayout& starter_layout();
// The layout with id `id`, or null.
const ShipLayout* find_layout(std::string_view id);

// The size of the smallest box holding all its hull (from (0, 0)).
GridSize layout_bounds(const ShipLayout& layout);
// Whether it has hull at `p` (layout coordinates).
bool layout_has_hull(const ShipLayout& layout, GridPos p);
// How many cells of hull it has.
int layout_hull_count(const ShipLayout& layout);
// Where its (0, 0) goes on the ship's build grid: centered on it.
GridPos layout_origin(const ShipLayout& layout);
// Where its cell `p` is on the ship's build grid.
GridPos layout_to_grid(const ShipLayout& layout, GridPos p);
// Whether the player can build it yet.
bool layout_unlocked(const ShipLayout& layout, const Progress& progress);
