// sector.h — the star map: the sectors of space the fleet can travel
// between, what each one has to gather, and how hostile it is.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// Sectors are linked to their neighbors; a ship jumps from one to a
// neighbor (see World::jump). Sector 0 is home, where a game starts: safe,
// with every raw resource. Further out, the yields vary (a sector may have
// no iron, or twice the iridium) and the threat grows.
//
// What the player knows: a sector a ship has been to is visited. Its
// neighbors are known (shown on the map, without details). A sector is
// surveyed, its yields and threat shown, once visited, or once a ship with a
// survey scanner is in a sector next to it.
#pragma once

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

// The waves of enemies a sector of `threat` sends before it's cleared (see
// combat.h): none for a safe one.
int waves_for_threat(int threat);

// A yield is a percentage of the home sector's: collectors there catch
// items that much as fast. 0: none to be had.
using Yields = std::array<int, kRawResourceCount>;

struct Sector {
    std::string name;
    float x = 0.0f;  // where it is drawn on the map (home at 0, 0)
    float y = 0.0f;
    std::vector<int> neighbors;  // indices of the sectors linked to it
    Yields yields{};
    int threat = 0;      // 0 (safe) to kMaxThreat
    int waves_left = 0;  // waves it has yet to send (see combat.h)
    bool visited = false;
    bool surveyed = false;

    // The yield of `kind`: 0 for a kind that isn't gathered.
    int yield(ItemKind kind) const;
    // Sends waves at the ships in it.
    bool hostile() const { return waves_left > 0; }
    // Was hostile, and has sent all its waves.
    bool cleared() const { return threat > 0 && waves_left == 0; }
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

// How many sectors StarMap::generate() makes.
constexpr int kStarMapSize = 24;
