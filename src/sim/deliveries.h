// deliveries.h — the game's goals: deliveries of goods to sectors of the
// star map, which pay out points, and the tech tree those points buy,
// unlocking recipes and bigger ship layouts (see ship_layout.h).
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// Deliveries come in chains, one chain per sector that takes them (see
// StarMap::delivery_sector): a sector asks for one delivery at a time, the
// next in its chain once the one before it is made, and each asks for more,
// and for items further up the production chain, than the one before. A
// delivery is open once the one before it in its chain is made and every
// item it asks for can be made (its recipe unlocked): so the open ones are
// spread over several sectors, for the player to pick between. The fleet's
// providers (see building.h) in that sector hand over what they hold of
// what it wants, and what's delivered is used up (see Fleet::tick).
// Completing one pays out its points (delivery_points), in proportion to
// the raw resources it took.
//
// Points buy techs, at the shop (no sector to visit): each tech has a price
// and comes after others, and unlocks the recipes making the items it
// lists. A recipe no tech lists is unlocked from the start, unless it makes
// the ammunition of a weapon the fleet has yet to find.
//
// Weapons have no recipes at all: they are found, salvaged from beaten enemy
// ships (see combat.h), and a new game starts with a burst laser. Finding a
// weapon unlocks its ammunition's recipe (the burst laser's focus crystals
// come with a tech instead).
#pragma once

#include "sim/game_types.h"
#include "sim/recipes.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

// What deliveries pay in, and the shop takes: a placeholder name, in one
// place so it's easy to change.
constexpr const char* kPointsName = "currency";
constexpr const char* kPointsShort = "currency";

struct Delivery {
    const char* name;
    int chain;                    // which chain (so which sector) it belongs to
    std::vector<ItemStack> wants;  // what to deliver
};

struct Tech {
    const char* name;
    int cost;                             // in points
    std::vector<std::string_view> after;  // the techs it comes after, by name
    std::vector<ItemKind> unlocks;        // the items whose recipes it unlocks
};

// How many delivery chains there are (each taken in its own sector).
constexpr int kDeliveryChains = 5;

// Every delivery, chain by chain, each chain in order.
const std::vector<Delivery>& all_deliveries();
// Every tech, each after the ones it comes after.
const std::vector<Tech>& all_techs();

// The index of the delivery (tech) named `name`, which must be one
// (asserted; without asserts, the table's size).
std::size_t delivery_index(std::string_view name);
std::size_t tech_index(std::string_view name);

// The deliveries of chain `chain`, in order.
std::vector<std::size_t> chain_deliveries(int chain);
// The delivery before `d` in its chain, if any.
std::optional<std::size_t> delivery_before(std::size_t d);
// What delivery `d` pays out: one point for every 20 raw resources its
// goods take, rounded (at least one).
int delivery_points(std::size_t d);
// What `count` of `kind` take in raw resources, all kinds together.
double raw_resources_in(ItemKind kind, double count);

// The indices of the techs that tech `t` comes after.
std::vector<std::size_t> tech_prerequisites(std::size_t t);

// The index of the tech that unlocks the recipe making `kind`, or nullopt
// if it is unlocked from the start.
std::optional<std::size_t> tech_unlocking(ItemKind kind);

// The weapon whose finding unlocks the recipe making `kind` (the
// ammunition only it fires), or nullopt.
std::optional<ItemKind> weapon_unlocking(ItemKind kind);

// How far the player is: the deliveries made, the points to spend, the
// techs bought, and the weapons found.
class Progress {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    // Per delivery: complete, and what has been delivered toward it, per
    // item kind.
    std::vector<bool> complete_;
    std::vector<std::array<int, kItemKindCount>> delivered_;
    int points_ = 0;  // to spend
    std::vector<bool> bought_;  // per tech
    // The kinds of weapon the fleet has found, in the order it did.
    std::vector<ItemKind> found;

public:
    Progress();

    // --- Deliveries ---
    // Whether delivery `d` is complete.
    bool done(std::size_t d) const { return d < complete_.size() && complete_[d]; }
    // Whether it can be worked on: not complete, the one before it in its
    // chain complete, and everything it asks for unlocked.
    bool open(std::size_t d) const;
    // The open deliveries, in table order.
    std::vector<std::size_t> open_deliveries() const;
    // The delivery chain `chain` is on: its first not complete (open or
    // waiting on a tech), or nullopt once all are.
    std::optional<std::size_t> chain_current(int chain) const;
    // How many are complete.
    std::size_t done_count() const;
    bool all_complete() const { return done_count() == complete_.size(); }
    // How many of `kind` have been delivered toward delivery `d`.
    int delivered(std::size_t d, ItemKind kind) const;
    // How many more of `kind` delivery `d` wants: 0 unless it's open and
    // asks for it.
    int wanted(std::size_t d, ItemKind kind) const;
    // Counts `count` of `kind` delivered toward delivery `d` (no more than
    // it still wants); true if that completed it, paying out its points.
    // Nothing happens unless it's open.
    bool deliver(std::size_t d, ItemKind kind, int count = 1);
    // Completes open delivery `d` at once, paying out (the debug menu);
    // false if it isn't open.
    bool complete(std::size_t d);

    // --- Points and the shop ---
    int points() const { return points_; }
    // Adds `n` points (the debug menu).
    void add_points(int n) { points_ += n; }
    // Whether tech `t` has been bought.
    bool bought(std::size_t t) const { return t < bought_.size() && bought_[t]; }
    // Whether it's for sale: not bought, and every one it comes after is.
    bool for_sale(std::size_t t) const;
    // For sale, and there are the points to pay for it.
    bool can_buy(std::size_t t) const;
    // Buys tech `t`, paying its cost; false (and no change) unless
    // can_buy(t).
    bool buy(std::size_t t);
    // Buys tech `t` without paying (tests, the debug menu); false if it
    // isn't for sale.
    bool grant(std::size_t t);
    std::size_t bought_count() const;

    // --- Weapons ---
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
