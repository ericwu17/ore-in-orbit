// sector.h — the star map: the sectors of space the fleet can travel
// between, what each one has to gather, and how hostile it is.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// Sectors are linked to their neighbors; a ship jumps from one to a
// neighbor (see World::jump). Sector 0 is home, where a game starts: safe,
// with every raw resource but iridium. Further out, the yields grow (a
// sector may lack one resource or another, but what it has comes faster
// the further it is from home) and so does the threat, and with it the
// enemy ships lying in wait (see enemies.h): a sector may hold one, or
// none, and one beaten is gone for good.
//
// Every sector's resources run out: each holds so many of each raw
// resource (its reserves), and every catch a collector makes there takes
// one. Once one is gone, it's gone for good: collectors aimed at it there
// stall, and the fleet has to go elsewhere for more. Home holds enough to
// reach travel (the "Escape velocity" milestone) with a margin; the
// other sectors hold more, the richer their yields.
//
// What the player knows: a sector a ship has been to is visited. Its
// neighbors are known (shown on the map, without details). A sector is
// surveyed, its yields, threat and enemy shown, once visited, or once a ship with a
// survey scanner is in a sector next to it.
#pragma once

#include "sim/enemies.h"
#include "sim/game_types.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The raw resources, the ones collectors gather, in ItemKind order: what a
// sector's yields are listed by.
constexpr ItemKind kRawResources[] = {ItemKind::IronOre, ItemKind::CopperOre, ItemKind::Silica, ItemKind::Carbon,
                                      ItemKind::IridiumOre};
constexpr int kRawResourceCount = static_cast<int>(std::size(kRawResources));

// Where `kind` is in kRawResources, or nullopt if it isn't a raw resource.
std::optional<int> raw_resource_index(ItemKind kind);

// The highest threat a sector has.
constexpr int kMaxThreat = 5;

// A yield is a percentage of the home sector's: collectors there catch
// items that much as fast. 0: none to be had.
using Yields = std::array<int, kRawResourceCount>;

// How many of each raw resource are left to gather in a sector, by
// kRawResources index. 0: run out (or never there).
using Reserves = std::array<int, kRawResourceCount>;

// What home starts with: enough of each to reach the "Escape velocity"
// milestone (fuel cells, to travel) with a factory of a fair size, a jump
// and a first fight's ammunition, and a margin over (see the readme's
// "Running out" section for the sums). No iridium: that's found elsewhere.
// Other sectors start with kSectorReserves scaled by their yields (see
// starting_reserves()).
constexpr Reserves kHomeReserves = {7000, 1200, 1100, 1700, 0};
// What a sector other than home holds of each at a yield of 100%: more
// than home, and the richer its yields, the more.
constexpr Reserves kSectorReserves = {10'500, 1800, 1650, 2550, 1500};

struct Sector {
    std::string name;
    float x = 0.0f;  // where it is drawn on the map (home at 0, 0)
    float y = 0.0f;
    std::vector<int> neighbors;  // indices of the sectors linked to it
    Yields yields{};
    // What's left to gather of each, from starting_reserves() down to 0.
    Reserves reserves{};
    int threat = 0;  // 0 (safe) to kMaxThreat
    // The enemy ship waiting there, until it's beaten (never at threat 0).
    std::optional<EnemyClass> enemy;
    bool beaten = false;  // an enemy was destroyed here
    bool visited = false;
    bool surveyed = false;

    // The yield of `kind`: 0 for a kind that isn't gathered.
    int yield(ItemKind kind) const;
    // How many of `kind` are left to gather: 0 for a kind that isn't
    // gathered, or has run out.
    int left(ItemKind kind) const;
    // Whether `kind` was there to gather and has all been gathered.
    bool run_out(ItemKind kind) const { return yield(kind) > 0 && left(kind) == 0; }
    // An enemy ship waits there: ships that come in have a fight.
    bool hostile() const { return enemy.has_value(); }
    // Its enemy was beaten.
    bool cleared() const { return beaten; }
};

class StarMap {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    std::vector<Sector> sectors;

public:
    // About kStarMapSize sectors scattered around home (sector 0, visited),
    // each linked to its nearest neighbors and all of them reachable, laid
    // out, named and stocked from `seed`: the same seed, the same map.
    static StarMap generate(std::uint64_t seed);

    int size() const { return static_cast<int>(sectors.size()); }
    const Sector& at(int i) const { return sectors[static_cast<std::size_t>(i)]; }
    Sector& at(int i) { return sectors[static_cast<std::size_t>(i)]; }
    const std::vector<Sector>& all() const { return sectors; }
    bool linked(int a, int b) const;
    // Visited, or next to a sector that is.
    bool known(int i) const;

    // A ship arrived in `i`: visited and surveyed from now on.
    void visit(int i);
    // A survey scanner next door: surveyed from now on.
    void survey(int i);
};

// What sector `s` holds of each raw resource before any is gathered: home's
// kHomeReserves (`home`), or kSectorReserves scaled by its yields (none of
// what it has none of). The same for a sector every time, so saves that
// predate reserves can be given them.
Reserves starting_reserves(const Sector& s, bool home);

// How many sectors StarMap::generate() makes.
constexpr int kStarMapSize = 24;
