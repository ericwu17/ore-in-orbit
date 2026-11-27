// sector.cpp — the star map and generating it (see sector.h).
#include "sim/sector.h"

#include "sim/deliveries.h"
#include "sim/rng.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <iterator>

std::optional<int> raw_resource_index(ItemKind kind) {
    for (int i = 0; i < kRawResourceCount; ++i)
        if (kRawResources[i] == kind)
            return i;
    return std::nullopt;
}

int Sector::left(ItemKind kind) const {
    const std::optional<int> i = raw_resource_index(kind);
    return i ? reserves[static_cast<std::size_t>(*i)] : 0;
}

bool StarMap::linked(int a, int b) const {
    const std::vector<int>& n = at(a).neighbors;
    return std::find(n.begin(), n.end(), b) != n.end();
}

bool StarMap::known(int i) const {
    if (at(i).visited)
        return true;
    for (const int n : at(i).neighbors)
        if (at(n).visited)
            return true;
    return false;
}

void StarMap::visit(int i) {
    sectors[static_cast<std::size_t>(i)].visited = true;
}

int StarMap::chain_sector(int chain) const {
    return chain >= 0 && static_cast<std::size_t>(chain) < chain_sectors.size()
               ? chain_sectors[static_cast<std::size_t>(chain)]
               : -1;
}

std::vector<std::size_t> StarMap::deliveries_at(int i, const Progress& progress) const {
    std::vector<std::size_t> here;
    for (std::size_t d = 0; d < progress.delivery_count(); ++d)
        if (delivery_sector(progress.delivery(d)) == i)
            here.push_back(d);
    return here;
}

static_assert(kSafeSectors >= kDeliveryChains, "a safe sector for each chain of deliveries");

namespace {

// The map's layout: sectors scattered over a disc this wide around home, no
// two closer than kMinSpacing.
constexpr float kMapRadius = 100.0f;
constexpr float kMinSpacing = 16.0f;

float distance(const Sector& a, const Sector& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

std::string make_name(Rng& rng) {
    static constexpr const char* kStarts[] = {"Kar", "Vel", "Or",  "Ix",  "Tau", "Ser", "Mon", "Dra", "Lys", "Quor",
                                              "Nem", "Bri", "Zan", "Hel", "Cor", "Phi", "Ast", "Ery", "Gal", "Ves"};
    static constexpr const char* kEnds[] = {"is", "on", "a", "ex", "ar", "eth", "ium", "os", "un"};
    static constexpr const char* kKinds[] = {"Reach", "Drift", "Expanse", "Verge", "Belt", "Deep", "Shoal", "Gate"};
    const auto pick = [&](const auto& list) {
        return list[static_cast<std::size_t>(rng.range(0, static_cast<int>(std::size(list)) - 1))];
    };
    return std::string(pick(kStarts)) + pick(kEnds) + " " + pick(kKinds);
}

void link(std::vector<Sector>& sectors, int a, int b) {
    std::vector<int>& na = sectors[static_cast<std::size_t>(a)].neighbors;
    if (a == b || std::find(na.begin(), na.end(), b) != na.end())
        return;
    na.push_back(b);
    sectors[static_cast<std::size_t>(b)].neighbors.push_back(a);
}

}  // namespace

StarMap StarMap::generate(std::uint64_t seed) {
    Rng rng{seed};
    StarMap map;
    std::vector<Sector>& sectors = map.sectors;

    // Home at the middle, the rest scattered around it.
    Sector home;
    home.name = "Home";
    home.reserves = kHomeReserves;
    sectors.push_back(home);
    for (int tries = 0; static_cast<int>(sectors.size()) < kStarMapSize && tries < 100'000; ++tries) {
        Sector s;
        const float angle = rng.unit() * 6.2831853f;
        const float r = std::sqrt(rng.unit()) * kMapRadius;
        s.x = std::cos(angle) * r;
        s.y = std::sin(angle) * r;
        const bool crowded = std::any_of(sectors.begin(), sectors.end(),
                                         [&](const Sector& other) { return distance(s, other) < kMinSpacing; });
        if (!crowded)
            sectors.push_back(s);
    }
    // Nearest home first, so the index grows outward.
    std::sort(sectors.begin() + 1, sectors.end(),
              [&](const Sector& a, const Sector& b) { return distance(a, home) < distance(b, home); });
    const int n = static_cast<int>(sectors.size());

    // Each linked to its two nearest, sometimes three.
    for (int i = 0; i < n; ++i) {
        std::vector<int> by_distance;
        for (int j = 0; j < n; ++j)
            if (j != i)
                by_distance.push_back(j);
        std::sort(by_distance.begin(), by_distance.end(), [&](int a, int b) {
            return distance(sectors[static_cast<std::size_t>(i)], sectors[static_cast<std::size_t>(a)]) <
                   distance(sectors[static_cast<std::size_t>(i)], sectors[static_cast<std::size_t>(b)]);
        });
        const int links = rng.chance(0.35f) ? 3 : 2;
        for (int k = 0; k < std::min(links, n - 1); ++k)
            link(sectors, i, by_distance[static_cast<std::size_t>(k)]);
    }
    // Then joined up: while some can't be reached from home, link the
    // closest pair across the gap.
    for (;;) {
        std::vector<bool> reached(static_cast<std::size_t>(n), false);
        std::vector<int> stack{0};
        reached[0] = true;
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            for (const int j : sectors[static_cast<std::size_t>(i)].neighbors)
                if (!reached[static_cast<std::size_t>(j)]) {
                    reached[static_cast<std::size_t>(j)] = true;
                    stack.push_back(j);
                }
        }
        int best_a = -1;
        int best_b = -1;
        float best = 0.0f;
        for (int a = 0; a < n; ++a)
            for (int b = 0; b < n; ++b) {
                if (!reached[static_cast<std::size_t>(a)] || reached[static_cast<std::size_t>(b)])
                    continue;
                const float d = distance(sectors[static_cast<std::size_t>(a)], sectors[static_cast<std::size_t>(b)]);
                if (best_a < 0 || d < best) {
                    best_a = a;
                    best_b = b;
                    best = d;
                }
            }
        if (best_a < 0)
            break;
        link(sectors, best_a, best_b);
    }

    // Names, resources, and threat growing with the distance from home.
    for (int i = 1; i < n; ++i) {
        Sector& s = sectors[static_cast<std::size_t>(i)];
        const float far = distance(s, home) / kMapRadius;  // 0 to 1
        for (bool unique = false; !unique;) {
            s.name = make_name(rng);
            unique = std::none_of(sectors.begin(), sectors.begin() + i,
                                  [&](const Sector& other) { return other.name == s.name; });
        }
        // Each resource, likely; iridium less so near home. Two at least.
        int present = 0;
        for (int r = 0; r < kRawResourceCount; ++r) {
            const bool iridium = kRawResources[r] == ItemKind::IridiumOre;
            if (!rng.chance(iridium ? 0.45f - far * 0.3f : 0.25f)) {
                s.reserves[static_cast<std::size_t>(r)] = kSectorReserves[static_cast<std::size_t>(r)];
                ++present;
            }
        }
        while (present < 2) {
            const auto r = static_cast<std::size_t>(rng.range(0, kRawResourceCount - 1));
            if (s.reserves[r] == 0) {
                s.reserves[r] = kSectorReserves[r];
                ++present;
            }
        }
        s.threat = std::clamp(static_cast<int>(far * 5.0f + rng.unit() * 1.2f - 0.3f), 0, kMaxThreat);
        // An enemy, more likely the higher the threat.
        if (s.threat > 0 && rng.chance(0.5f + 0.1f * static_cast<float>(s.threat))) {
            const std::vector<EnemyClass> classes = enemies_for_threat(s.threat);
            s.enemy = classes[static_cast<std::size_t>(rng.range(0, static_cast<int>(classes.size()) - 1))];
        }
    }
    // Everything home lacks, or will run out of, is a jump away: each raw
    // resource in at least one of home's neighbors (the nearest of them,
    // if none has it).
    for (int r = 0; r < kRawResourceCount; ++r) {
        const std::vector<int>& next_door = sectors[0].neighbors;
        const auto i = static_cast<std::size_t>(r);
        const auto has = [&](int s) { return sectors[static_cast<std::size_t>(s)].reserves[i] > 0; };
        if (!next_door.empty() && std::none_of(next_door.begin(), next_door.end(), has)) {
            const int nearest = *std::min_element(next_door.begin(), next_door.end());  // the index grows outward
            sectors[static_cast<std::size_t>(nearest)].reserves[i] = kSectorReserves[i];
        }
    }

    // The safe sectors: home's neighbors, then theirs, and so on, the
    // nearest first, kSafeSectors of them.
    std::vector<int> hops(static_cast<std::size_t>(n), -1);
    hops[0] = 0;
    for (std::deque<int> queue{0}; !queue.empty(); queue.pop_front())
        for (const int j : sectors[static_cast<std::size_t>(queue.front())].neighbors)
            if (hops[static_cast<std::size_t>(j)] < 0) {
                hops[static_cast<std::size_t>(j)] = hops[static_cast<std::size_t>(queue.front())] + 1;
                queue.push_back(j);
            }
    std::vector<int> safe;
    for (int i = 1; i < n; ++i)
        safe.push_back(i);
    std::stable_sort(safe.begin(), safe.end(), [&](int a, int b) {
        return hops[static_cast<std::size_t>(a)] < hops[static_cast<std::size_t>(b)];
    });  // the index grows outward, so the nearest come first within a hop
    safe.resize(std::min(safe.size(), static_cast<std::size_t>(kSafeSectors)));
    for (const int i : safe) {
        Sector& s = sectors[static_cast<std::size_t>(i)];
        s.threat = 0;
        s.enemy.reset();
        s.reserves = kSectorReserves;  // all of everything: none runs short of a kind
    }

    // Where each delivery chain is taken: each in its own safe sector, the
    // first nearest home (sharing the furthest, were there too few).
    map.chain_sectors.assign(kDeliveryChains, 0);
    if (!safe.empty())
        for (std::size_t c = 0; c < map.chain_sectors.size(); ++c)
            map.chain_sectors[c] = safe[std::min(c, safe.size() - 1)];
    map.visit(0);
    return map;
}
