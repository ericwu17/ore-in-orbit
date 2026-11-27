// deliveries.h — the game's goals: deliveries of goods to sectors of the
// star map, which pay out points, and the tech tree those points buy,
// unlocking recipes and bigger ship layouts (see ship_layout.h).
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// Deliveries come in chains, one chain per sector that takes them (see
// StarMap::chain_sector): a sector asks for one delivery at a time, the
// next in its chain once the one before it is made. No delivery is written
// out by hand: each game makes its own (Progress::Progress, from a seed),
// each chain from its sector's line of goods (its theme).
//
// Most chains are finite: kFiniteChainLength deliveries, made up when the
// game starts, each asking for more, and for items further up the
// production chain (so waiting on later techs), than the one before. A
// delivery is open once the one before it in its chain is made and every
// item it asks for can be made (its recipe unlocked): so the open ones are
// spread over several sectors, for the player to pick between.
//
// The others are endless (chain_endless): standing orders, one at a time,
// a new one made up as soon as the last is made, from what the player can
// make by then, so it's open at once. Each asks for more the more points
// the player has earned all game (Progress::earned): endless_target_points.
//
// The fleet's providers (see building.h) in that sector hand over what
// they hold of what it wants, and what's delivered is used up (see
// Fleet::tick). Completing one pays out its points, in proportion to the raw
// resources it took (delivery_points).
//
// Points buy techs, at the shop (no sector to visit): each tech has a price
// and comes after others (a wide tree: several to choose from at each
// step), and unlocks the recipes making the items it lists, or a ship
// layout. A recipe no tech lists is unlocked from the start, unless it makes
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
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// What deliveries pay in, and the shop takes: a placeholder name, in one
// place so it's easy to change.
constexpr const char* kPointsName = "currency";
constexpr const char* kPointsShort = "currency";

struct Delivery {
    std::string name;
    int chain = 0;                 // which chain (so which sector) it belongs to
    std::vector<ItemStack> wants;  // what to deliver
    int points = 0;                // what it pays (delivery_points of `wants`)
};

struct Tech {
    const char* name;
    int cost;                             // in points
    std::vector<std::string_view> after;  // the techs it comes after, by name
    std::vector<ItemKind> unlocks;        // the items whose recipes it unlocks
    const char* blurb;                    // what it's good for, in a few words
};

// How many delivery chains there are (each taken in its own sector).
constexpr int kDeliveryChains = 5;
// How many deliveries a finite chain has.
constexpr int kFiniteChainLength = 4;

// Whether chain `chain` is endless (a standing order: a new delivery after
// each one made), rather than kFiniteChainLength deliveries.
bool chain_endless(int chain);
// What the sector taking chain `chain` is, in a word or two ("Shipyard").
const char* chain_role(int chain);
// The items chain `chain` may ask for, any step.
std::vector<ItemKind> chain_items(int chain);

// Every tech, each after the ones it comes after.
const std::vector<Tech>& all_techs();
// The index of the tech named `name`, which must be one (asserted; without
// asserts, the table's size).
std::size_t tech_index(std::string_view name);

// Raw resources a delivery's goods take for each point it pays.
constexpr double kRawPerPoint = 25.0;
// What a delivery asking for `wants` pays out: one point for every
// kRawPerPoint raw resources its goods take, rounded (at least one).
int delivery_points(const std::vector<ItemStack>& wants);
// What `count` of `kind` take in raw resources, all kinds together.
double raw_resources_in(ItemKind kind, double count);

// What the finite chains' deliveries aim to pay, step by step (the goods
// are rounded to whole fives, so they pay about this).
constexpr std::array<int, kFiniteChainLength> kFiniteStepPoints = {5, 11, 20, 34};
// What an endless chain's next delivery aims to pay: a base, plus a share
// of every point earned so far, so standing orders keep up with the
// factory that made them.
constexpr int kEndlessBasePoints = 6;
constexpr double kEndlessEarnedShare = 0.1;
int endless_target_points(long long earned);

// The indices of the techs that tech `t` comes after.
std::vector<std::size_t> tech_prerequisites(std::size_t t);
// How deep tech `t` is in the tree: 1 with none before it, otherwise one
// more than the deepest it comes after.
int tech_tier(std::size_t t);

// The index of the tech that unlocks the recipe making `kind`, or nullopt
// if it is unlocked from the start.
std::optional<std::size_t> tech_unlocking(ItemKind kind);

// The weapon whose finding unlocks the recipe making `kind` (the
// ammunition only it fires), or nullopt.
std::optional<ItemKind> weapon_unlocking(ItemKind kind);

// How far the player is: the deliveries made up so far and how far along
// each is, the points to spend and earned all game, the techs bought, and
// the weapons found.
class Progress {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    // What the deliveries are made up from: the same seed, the same
    // deliveries (for the same play, as endless ones depend on it).
    std::uint64_t seed_ = 0;
    // Every delivery made up so far, chain by chain in the order they
    // were (so an index stays put once given out); per delivery, complete,
    // and what has been delivered toward it, per item kind.
    std::vector<Delivery> deliveries_;
    std::vector<bool> complete_;
    std::vector<std::array<int, kItemKindCount>> delivered_;
    int points_ = 0;          // to spend
    long long earned_ = 0;    // every point ever paid out (or given)
    std::vector<bool> bought_;  // per tech
    // The kinds of weapon the fleet has found, in the order it did.
    std::vector<ItemKind> found;

    // Adds the next delivery of each endless chain with none left to make.
    void extend_endless();
    // Delivery `d` complete: pays out, and makes up the next of an endless
    // chain.
    void finish(std::size_t d);

public:
    // A new game's progress: every finite chain's deliveries and each
    // endless chain's first, made up from `seed`.
    explicit Progress(std::uint64_t seed = 0);
    // Makes the deliveries up afresh from `seed`, as a new game does (a new
    // World, for its map's seed), dropping any made so far.
    void make_up_deliveries(std::uint64_t seed);

    // --- Deliveries ---
    const std::vector<Delivery>& deliveries() const { return deliveries_; }
    const Delivery& delivery(std::size_t d) const { return deliveries_[d]; }
    std::size_t delivery_count() const { return deliveries_.size(); }
    // The deliveries of chain `chain` so far, in order.
    std::vector<std::size_t> chain_deliveries(int chain) const;
    // The delivery before `d` in its chain, if any.
    std::optional<std::size_t> delivery_before(std::size_t d) const;
    // Whether delivery `d` is complete.
    bool done(std::size_t d) const { return d < complete_.size() && complete_[d]; }
    // Whether it can be worked on: not complete, the one before it in its
    // chain complete, and everything it asks for unlocked.
    bool open(std::size_t d) const;
    // The open deliveries, in order.
    std::vector<std::size_t> open_deliveries() const;
    // The delivery chain `chain` is on: its first not complete (open or
    // waiting on a tech), or nullopt once a finite one is done.
    std::optional<std::size_t> chain_current(int chain) const;
    // How many are complete.
    std::size_t done_count() const;
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
    // The deliveries not yet made asking for something tech `t` unlocks
    // (which wait for it).
    std::vector<std::size_t> deliveries_opened_by(std::size_t t) const;

    // --- Points and the shop ---
    int points() const { return points_; }
    // Every point earned all game, spent or not: what endless deliveries
    // grow with.
    long long earned() const { return earned_; }
    // Adds `n` points (the debug menu), earned like any others.
    void add_points(int n) {
        points_ += n;
        earned_ += n;
    }
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
