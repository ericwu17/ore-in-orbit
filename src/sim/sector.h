// sector.h — the star map: the sectors of space the fleet can travel
// between, what each one has to gather, and how hostile it is.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// Sectors are linked to their neighbors; a ship jumps from one to a
// neighbor (see World::jump). Sector 0 is home, where a game starts: safe,
// with every raw resource. Around home lie kSafeSectors more safe sectors
// (home's neighbors, then the nearest of theirs): no threat, no enemy, and
// every raw resource too. Further out a sector may lack one resource or
// another, and the threat grows, and with it the enemy ships lying in wait
// (see enemies.h): a sector may hold one, or none, and one beaten is gone
// for good.
//
// The deliveries (see deliveries.h) are each taken in one of the safe
// sectors: the first ones next to home, the later ones further out, and
// the deliveries open at the same time in different sectors where there
// are enough of them.
//
// Every sector's resources run out: each holds so many of each raw
// resource (its reserves), and every catch a collector makes there takes
// one. Once one is gone, it's gone for good: collectors aimed at it there
// stall, and the fleet has to go elsewhere for more. Home holds enough to
// reach the "Escape velocity" delivery with a margin even with every
// delivery paid for from home, and travel (fuel cells, from the start)
// with a wide one; the other sectors hold more of what they have.
//
// What the player knows: a sector a ship has been to is visited. It and
// its neighbors are known: shown on the map, with their resources, threat
// and enemy.
#pragma once

#include "sim/enemies.h"
#include "sim/game_types.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The raw resources, the ones collectors gather, in ItemKind order: what a
// sector's reserves are listed by.
constexpr ItemKind kRawResources[] = {ItemKind::IronOre, ItemKind::CopperOre, ItemKind::Silica, ItemKind::Carbon,
                                      ItemKind::IridiumOre};
constexpr int kRawResourceCount = static_cast<int>(std::size(kRawResources));

// Where `kind` is in kRawResources, or nullopt if it isn't a raw resource.
std::optional<int> raw_resource_index(ItemKind kind);

// The highest threat a sector has.
constexpr int kMaxThreat = 5;

// How many of each raw resource are left to gather in a sector, by
// kRawResources index. 0: run out (or never there).
using Reserves = std::array<int, kRawResourceCount>;

// What home starts with: enough of each to reach the "Escape velocity"
// delivery (weapons) with a factory of a fair size, a jump and a first
// fight's ammunition, and a margin over (see the readme's
// "Running out" section for the sums); and some of everything, iridium
// included, so no recipe is out of reach at home.
constexpr Reserves kHomeReserves = {7000, 1200, 1100, 1700, 1000};
// What a sector other than home starts with of each resource it has (none
// of the others): more than home.
constexpr Reserves kSectorReserves = {10'500, 1800, 1650, 2550, 1500};

struct Sector {
    std::string name;
    float x = 0.0f;  // where it is drawn on the map (home at 0, 0)
    float y = 0.0f;
    std::vector<int> neighbors;  // indices of the sectors linked to it
    // What's left to gather of each, from kHomeReserves (home) or
    // kSectorReserves (of what it has) down to 0.
    Reserves reserves{};
    int threat = 0;  // 0 (safe) to kMaxThreat
    // The enemy ship waiting there, until it's beaten (never at threat 0).
    std::optional<EnemyClass> enemy;
    bool beaten = false;  // an enemy was destroyed here
    bool visited = false;

    // How many of `kind` are left to gather: 0 for a kind that isn't
    // gathered, or none of which is left there.
    int left(ItemKind kind) const;
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
    // The sector each delivery is taken in, by delivery index.
    std::vector<int> delivery_sectors;

public:
    // About kStarMapSize sectors scattered around home (sector 0, visited),
    // each linked to its nearest neighbors and all of them reachable, laid
    // out, named and stocked from `seed`: the same seed, the same map. The
    // safe sectors around home, and where each delivery is taken, too.
    static StarMap generate(std::uint64_t seed);

    int size() const { return static_cast<int>(sectors.size()); }
    const Sector& at(int i) const { return sectors[static_cast<std::size_t>(i)]; }
    Sector& at(int i) { return sectors[static_cast<std::size_t>(i)]; }
    const std::vector<Sector>& all() const { return sectors; }
    bool linked(int a, int b) const;
    // Visited, or next to a sector that is: shown on the map, with what's
    // there.
    bool known(int i) const;

    // A ship arrived in `i`: visited from now on.
    void visit(int i);

    // The sector delivery `d` is taken in (-1 if the map has none for it).
    int delivery_sector(std::size_t d) const;
    // The deliveries taken in sector `i`, in table order.
    std::vector<std::size_t> deliveries_at(int i) const;
};

// How many sectors StarMap::generate() makes.
constexpr int kStarMapSize = 24;
// How many safe sectors (threat 0, never an enemy) it makes around home,
// home not counted.
constexpr int kSafeSectors = 6;
