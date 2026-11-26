// build_history.h — undoing the player's build actions on a ship (Ctrl/Cmd+Z).
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// An action is whatever one gesture did: a click or a drag that placed or
// removed buildings (belts turned round corners included), a blueprint
// paste, a Shift-drag of settings, or one change in the inspector (a
// recipe, a collector's target, a filter, running only on request, the
// bridge's item requests). It is kept as the change it made to each
// building it touched, by top-left cell: what stood there before (kind,
// facing and settings, as a blueprint piece) and what stands there after.
// Never what the buildings held: undoing puts the setup back, not the
// items, as a blueprint would.
//
// Recording: before changing anything, the caller touch()es the cell it
// changes (or touch_requests() the bridge's requests); commit() then closes
// the action, comparing each touched cell with how it stands now. A drag
// stays one action by committing only once it ends.
//
// Undoing the last action takes away what it built (refunded, with what it
// held, as a removal is), puts back what it removed (taking each building
// from the inventory again, as placing does) and sets the settings back.
// It is all or nothing, and only while the ship still stands as the action
// left it: if a touched building has since been destroyed, or changed by
// something else, or the inventory no longer has the buildings to put back,
// or something now stands in their way, the action can't be undone. Then
// nothing changes and the action is dropped (with why), so the next undo
// goes on to the one before.
#pragma once

#include "sim/blueprint.h"
#include "sim/game_types.h"
#include "sim/ship.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// What one action did to the building at one top-left cell. `before` and
// `after` keep their offset at that cell; nullopt: no building there.
struct BuildChange {
    GridPos at;
    std::optional<BlueprintBuilding> before;
    std::optional<BlueprintBuilding> after;
};

struct BuildAction {
    std::vector<BuildChange> changes;  // in the order they were touched
    // The bridge's item requests, before and after; both empty unless the
    // action changed them.
    bool requests_changed = false;
    std::vector<ItemStack> requests_before;
    std::vector<ItemStack> requests_after;
};

// What undoing an action would do, for the player: "placing 3 buildings",
// "removing 1 building, changing 2 settings".
std::string describe(const BuildAction& action);

enum class UndoResult {
    Ok,
    NothingToUndo,
    Changed,         // a building it touched has changed since (destroyed, or rebuilt)
    Blocked,         // something now stands where a building would go back
    NoneInInventory, // the inventory lacks the buildings to put back
};

struct UndoOutcome {
    UndoResult result = UndoResult::NothingToUndo;
    BuildAction action;              // the action undone (or dropped)
    std::vector<ItemStack> missing;  // NoneInInventory: what the inventory is short of
    int settings_skipped = 0;        // recipes no longer allowed, left unset
};

// A player-facing line for an undo: "Undone: placing 3 buildings", or why
// not ("Can't undo placing 3 buildings: one has been destroyed since").
std::string describe(const UndoOutcome& outcome);

// The building anchored at `at` as a blueprint piece, if one the player
// built is there (never the bridge or an engine).
std::optional<BlueprintBuilding> building_record(const Ship& ship, GridPos at);

// One ship's undo history (the UI keeps one per ship; it isn't saved).
class BuildHistory {
private:
    std::vector<BuildAction> done;  // oldest first
    // The action being recorded: each touched cell's change, `after` not
    // filled in until commit().
    BuildAction open;
    bool is_open = false;

public:
    // The most actions kept: older ones are forgotten.
    static constexpr std::size_t kMaxActions = 200;

    // Notes, before it changes, the building covering `cell` (or the cell,
    // if bare: a building is about to go there), unless already noted for
    // the open action.
    void touch(const Ship& ship, GridPos cell);
    // The same for the bridge's item requests.
    void touch_requests(const Bridge& bridge);
    // Closes the open action, keeping it if it changed anything; true if it
    // did.
    bool commit(const Ship& ship);
    // Whether an action is being recorded.
    bool recording() const { return is_open; }

    // Commits, then undoes the last action (see the top of this file).
    UndoOutcome undo(Ship& ship);
    // How many actions can be undone (the open one not counted).
    std::size_t size() const { return done.size(); }
    void clear();
};
