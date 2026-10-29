// milestones.h — the game's goals: deliver items to the bridge to unlock the
// next tier of recipes.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// The milestones come one at a time, in order. Items count toward the
// current one when a belt (or building) delivers them into the bridge, not
// when they are hand-crafted or refunded; they still go into the inventory,
// so nothing is used up. Completing one unlocks the recipes making the items
// it lists. A recipe no milestone lists is unlocked from the start.
#pragma once

#include "sim/game_types.h"
#include "sim/recipes.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

struct Milestone {
    const char* name;
    std::vector<ItemStack> deliver;  // what to deliver to the bridge
    std::vector<ItemKind> unlocks;   // the items whose recipes it unlocks
};

// Every milestone, in the order they come.
const std::vector<Milestone>& all_milestones();

// The index of the milestone named `name`, which must be one (asserted;
// without asserts, all_milestones().size(), which no progress reaches past).
// For tables that refer to a milestone, so reordering them can't shift what
// they mean.
std::size_t milestone_index(std::string_view name);

// The index of the milestone that unlocks the recipe making `kind`, or
// nullopt if it is unlocked from the start.
std::optional<std::size_t> milestone_unlocking(ItemKind kind);

// How far the player is through the milestones.
class Progress {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    std::size_t current = 0;  // index of the milestone being worked on
    // Delivered toward it, per item kind, each capped at what it needs.
    std::array<int, kItemKindCount> delivered{};

public:
    // The index of the milestone being worked on (all_milestones().size()
    // once every one is complete).
    std::size_t current_index() const { return current; }
    // The milestone being worked on, or null once every one is complete.
    const Milestone* current_milestone() const;
    bool all_complete() const { return current_milestone() == nullptr; }
    // How many of `kind` have been delivered toward the current one.
    int delivered_count(ItemKind kind) const { return delivered[static_cast<std::size_t>(kind)]; }

    // Counts one item delivered to the bridge; true if that completed the
    // current milestone (and moved on to the next).
    bool on_delivered(ItemKind kind);
    // Completes the current milestone at once (the debug menu); false if
    // every one already is.
    bool complete_current();

    // Whether the recipe making `kind` (and so the recipe itself, by its
    // main output) has been unlocked.
    bool unlocked(ItemKind kind) const;
    bool unlocked(const Recipe& recipe) const { return unlocked(recipe.outputs.front().kind); }
};
