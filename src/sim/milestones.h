// milestones.h — the game's goals: produce items to unlock the next tier of
// recipes, and bigger ship layouts (see ship_layout.h).
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// The milestones come one at a time, in order. Items count toward the
// current one when they are produced: when a machine on any of the fleet's
// ships, or a bridge hand-crafting, finishes a craft making them. Where they
// go afterwards doesn't matter, and nothing is used up; moving items about
// (belts, unloaders, cargo pods, refunds) counts for nothing. Completing one
// unlocks the recipes making the items it lists. A recipe no milestone lists
// is unlocked from the start, unless it makes the ammunition of a weapon the
// fleet has yet to find.
//
// Weapons have no recipes at all: they are found, salvaged from beaten enemy
// ships (see combat.h), and a new game starts with a burst laser. Finding a
// weapon unlocks its ammunition's recipe (the burst laser's focus crystals
// come with a milestone instead).
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
    std::vector<ItemStack> produce;  // what to produce
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

// The weapon whose finding unlocks the recipe making `kind` (the
// ammunition only it fires), or nullopt.
std::optional<ItemKind> weapon_unlocking(ItemKind kind);

// How far the player is through the milestones, and the weapons found.
class Progress {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    std::size_t current = 0;  // index of the milestone being worked on
    // Produced toward it, per item kind.
    std::array<int, kItemKindCount> produced{};
    // The kinds of weapon the fleet has found, in the order it did.
    std::vector<ItemKind> found;

public:
    // The index of the milestone being worked on (all_milestones().size()
    // once every one is complete).
    std::size_t current_index() const { return current; }
    // The milestone being worked on, or null once every one is complete.
    const Milestone* current_milestone() const;
    bool all_complete() const { return current_milestone() == nullptr; }
    // How many of `kind` have been produced toward the current one.
    int produced_count(ItemKind kind) const { return produced[static_cast<std::size_t>(kind)]; }

    // Counts `count` of `kind` produced; true if that completed the current
    // milestone (and moved on to the next). What's made past the last item a
    // milestone needs doesn't carry over to the next.
    bool on_produced(ItemKind kind, int count = 1);
    // Counts every stack of a craft's outputs; true if that completed the
    // current milestone.
    bool on_produced(const std::vector<ItemStack>& stacks);
    // Completes the current milestone at once (the debug menu); false if
    // every one already is.
    bool complete_current();

    // Whether the fleet has found a weapon of kind `weapon`.
    bool has_found(ItemKind weapon) const;
    const std::vector<ItemKind>& found_weapons() const { return found; }
    // Finds a weapon of kind `weapon`, unlocking its ammunition; false if
    // it's no weapon, or one already found.
    bool add_found(ItemKind weapon);
    // Whether the recipe making `kind` (and so the recipe itself, by its
    // main output) has been unlocked; for a weapon, which has none, whether
    // it has been found.
    bool unlocked(ItemKind kind) const;
    bool unlocked(const Recipe& recipe) const { return unlocked(recipe.outputs.front().kind); }
};
