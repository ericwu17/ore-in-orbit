// deliveries.h — the game's goals: deliveries of goods to sectors of the
// star map, each unlocking recipes, and bigger ship layouts (see
// ship_layout.h).
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// Each delivery asks for so many of a few items, to be delivered in one
// sector of the map (see StarMap::delivery_sector): the fleet's providers
// (see building.h) in that sector hand over what they hold of them, one
// item every kLinkTicks each, and what's delivered is used up (see
// Fleet::tick). Completing one unlocks the recipes making the items it
// lists, and opens the deliveries that come after it: a delivery is open
// once every one it comes after is complete, so more than one can be open
// at once, in different sectors, and the player picks which to work on.
// The later ones ask for more, and for items further up the production
// chain. A recipe no delivery lists is unlocked from the start, unless it
// makes the ammunition of a weapon the fleet has yet to find.
//
// Weapons have no recipes at all: they are found, salvaged from beaten enemy
// ships (see combat.h), and a new game starts with a burst laser. Finding a
// weapon unlocks its ammunition's recipe (the burst laser's focus crystals
// come with a delivery instead).
#pragma once

#include "sim/game_types.h"
#include "sim/recipes.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

struct Delivery {
    const char* name;
    std::vector<std::string_view> after;  // the deliveries it comes after, by name
    std::vector<ItemStack> wants;         // what to deliver
    std::vector<ItemKind> unlocks;        // the items whose recipes it unlocks
};

// Every delivery, each after the ones it comes after.
const std::vector<Delivery>& all_deliveries();

// The index of the delivery named `name`, which must be one (asserted;
// without asserts, all_deliveries().size()). For tables that refer to a
// delivery, so reordering them can't shift what they mean.
std::size_t delivery_index(std::string_view name);

// The indices of the deliveries that delivery `d` comes after.
std::vector<std::size_t> delivery_prerequisites(std::size_t d);

// How deep delivery `d` is in the tree: 1 for one that comes after none,
// otherwise one more than the deepest it comes after.
int delivery_tier(std::size_t d);

// The deepest tier any delivery has.
int max_delivery_tier();

// The index of the delivery that unlocks the recipe making `kind`, or
// nullopt if it is unlocked from the start.
std::optional<std::size_t> delivery_unlocking(ItemKind kind);

// The weapon whose finding unlocks the recipe making `kind` (the
// ammunition only it fires), or nullopt.
std::optional<ItemKind> weapon_unlocking(ItemKind kind);

// How far the player is through the deliveries, and the weapons found.
class Progress {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    // Per delivery: complete, and what has been delivered toward it, per
    // item kind.
    std::vector<bool> complete_;
    std::vector<std::array<int, kItemKindCount>> delivered_;
    // The kinds of weapon the fleet has found, in the order it did.
    std::vector<ItemKind> found;

public:
    Progress();

    // Whether delivery `d` is complete.
    bool done(std::size_t d) const { return d < complete_.size() && complete_[d]; }
    // Whether it can be worked on: not complete, and every one it comes
    // after is.
    bool open(std::size_t d) const;
    // The open deliveries, in table order.
    std::vector<std::size_t> open_deliveries() const;
    // How many are complete.
    std::size_t done_count() const;
    bool all_complete() const { return done_count() == complete_.size(); }

    // How many of `kind` have been delivered toward delivery `d`.
    int delivered(std::size_t d, ItemKind kind) const;
    // How many more of `kind` delivery `d` wants: 0 unless it's open and
    // asks for it.
    int wanted(std::size_t d, ItemKind kind) const;

    // Counts `count` of `kind` delivered toward delivery `d` (no more than
    // it still wants); true if that completed it. Nothing happens unless
    // it's open.
    bool deliver(std::size_t d, ItemKind kind, int count = 1);
    // Completes open delivery `d` at once (the debug menu); false if it
    // isn't open.
    bool complete(std::size_t d);

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
