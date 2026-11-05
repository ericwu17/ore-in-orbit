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
// A layout also has ports: 1x1 tiles of its hull, weapon ports and shield
// ports. A weapon is built only over a weapon port, a shield projector only
// over a shield port, and nothing else over either, so a ship holds no more
// of them than its layout has ports, however much hull it has. Ports are at
// least kPortSpacing apart, so no building covers two.
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
    std::vector<GridPos> weapon_ports;     // each weapon port's cell
    std::vector<GridPos> shield_ports;     // each shield port's cell
};

// How far apart (in x or y) any two ports of a layout are, at least: more
// than the biggest building is across, less one.
constexpr int kPortSpacing = 3;

// The two kinds of port: what a weapon or a shield projector takes.
enum class Port { Weapon, Shield };
// The port a building of kind `kind` takes, if any.
std::optional<Port> port_of(ItemKind kind);
// The cells of `layout`'s ports of kind `port` (layout coordinates).
const std::vector<GridPos>& layout_ports(const ShipLayout& layout, Port port);
// "weapon" or "shield".
const char* port_name(Port port);

// Every layout, smallest first.
const std::vector<ShipLayout>& all_layouts();
// The layout a new game's flagship has: the smallest.
const ShipLayout& starter_layout();
// The enemy ships' layouts (see enemies.h), never the player's to build.
const std::vector<ShipLayout>& enemy_layouts();
// The layout with id `id` (the player's or an enemy's), or null.
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
