// blueprint.h — a copy of part of a ship (the buildings on it, with their
// settings), to paste elsewhere, as one piece.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// A blueprint holds what a player sets up, never what is going on: each
// building's kind, place, facing and settings (a machine's recipe, a
// collector's target, a sorter's filter), but no items, buffers or progress,
// which pasting would otherwise conjure up. The layout's own parts (the
// bridge and engines) are never in one, nor is hull: the ship's is fixed,
// and pasting needs hull under every building.
//
// Pasting is all or nothing: the buildings in the order they were placed,
// each checked as though everything before it were already down
// (Ship::can_place with PlannedChanges). An identical building already in
// place counts as done and costs nothing. Only if every piece fits and the
// inventory has everything is any of it placed.
#pragma once

#include "sim/game_types.h"
#include "sim/recipes.h"
#include "sim/ship.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct BlueprintBuilding {
    ItemKind kind = ItemKind::TransportBelt;
    GridPos offset;                         // its top-left cell, from the blueprint's
    Direction facing = Direction::North;    // North for those that don't turn
    const Recipe* recipe = nullptr;         // machines
    std::optional<ItemKind> target;         // collectors
    std::optional<ItemKind> filter;         // sorters

    bool operator==(const BlueprintBuilding&) const = default;
};

struct Blueprint {
    std::string name;  // empty until the player names it
    GridSize size;     // the box around its buildings
    std::vector<BlueprintBuilding> buildings;  // in the order they were placed

    bool operator==(const Blueprint&) const = default;
};

// The buildings in the rectangle with corners `a` and `b` (either way
// round): a building partly inside it comes whole. Trimmed to what it holds;
// nullopt if that's nothing.
std::optional<Blueprint> copy_region(const Ship& ship, GridPos a, GridPos b);

// The blueprint turned a quarter clockwise (every placeable building is
// square or rotatable, so any blueprint can be).
Blueprint rotated_cw(const Blueprint& blueprint);

// One piece of a paste: a building, where it goes, and whether it can go
// there.
struct PasteEntry {
    GridPos at;
    ItemKind kind = ItemKind::TransportBelt;
    std::size_t building = 0;               // its index in the blueprint
    Direction facing = Direction::North;
    bool satisfied = false;                 // already there: nothing to do
    BuildResult result = BuildResult::Ok;   // NoneInInventory: fits, but can't be paid for
};

struct PastePlan {
    std::vector<PasteEntry> entries;  // in the order paste() places them
    std::vector<ItemStack> missing;   // what the inventory is short of
    int blocked = 0;                  // entries that don't fit, whatever the inventory
    bool ok() const { return blocked == 0 && missing.empty(); }
};

// What pasting `blueprint` with its top-left cell at `origin` would do.
PastePlan plan_paste(const Ship& ship, const Blueprint& blueprint, GridPos origin);
// Why a paste can't happen, for the player ("Something is already built
// there (3 in the way); missing 2 Transport belt"); "Ok" if it can.
std::string describe(const PastePlan& plan);

struct PasteResult {
    PastePlan plan;
    bool pasted = false;
    int settings_skipped = 0;  // recipes not unlocked yet, left unset
};
// Pastes it if plan_paste() says it all fits; otherwise changes nothing.
PasteResult paste(Ship& ship, const Blueprint& blueprint, GridPos origin);
