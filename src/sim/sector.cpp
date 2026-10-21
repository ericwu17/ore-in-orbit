// sector.cpp — the star map and generating it (see sector.h).
#include "sim/sector.h"

#include "sim/rng.h"

#include <algorithm>
#include <cmath>
#include <iterator>

std::optional<int> raw_resource_index(ItemKind kind) {
    for (int i = 0; i < kRawResourceCount; ++i)
        if (kRawResources[i] == kind)
            return i;
    return std::nullopt;
}

int Sector::yield(ItemKind kind) const {
    const std::optional<int> i = raw_resource_index(kind);
    return i ? yields[static_cast<std::size_t>(*i)] : 0;
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
    Sector& s = sectors[static_cast<std::size_t>(i)];
    s.visited = true;
    s.surveyed = true;
}

void StarMap::survey(int i) {
    sectors[static_cast<std::size_t>(i)].surveyed = true;
}

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
    home.yields.fill(100);
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

    // Names, yields and threat, growing with the distance from home.
    for (int i = 1; i < n; ++i) {
        Sector& s = sectors[static_cast<std::size_t>(i)];
        const float far = distance(s, home) / kMapRadius;  // 0 to 1
        for (bool unique = false; !unique;) {
            s.name = make_name(rng);
            unique = std::none_of(sectors.begin(), sectors.begin() + i,
                                  [&](const Sector& other) { return other.name == s.name; });
        }
        int present = 0;
        for (int r = 0; r < kRawResourceCount; ++r) {
            const bool iridium = kRawResources[r] == ItemKind::IridiumOre;
            int& y = s.yields[static_cast<std::size_t>(r)];
            if (rng.chance(iridium ? 0.55f - far * 0.3f : 0.3f))
                y = 0;
            else
                y = 50 + 25 * rng.range(0, 4) + 25 * static_cast<int>(far * 4.0f);  // richer further out
            present += y > 0 ? 1 : 0;
        }
        while (present < 2) {
            int& y = s.yields[static_cast<std::size_t>(rng.range(0, kRawResourceCount - 1))];
            if (y == 0) {
                y = 100;
                ++present;
            }
        }
        s.threat = std::clamp(static_cast<int>(far * 5.0f + rng.unit() * 1.2f - 0.3f), 0, kMaxThreat);
        // Home's neighbors are an easy first step.
        if (map.linked(0, i))
            s.threat = std::min(s.threat, 1);
    }
    map.visit(0);
    return map;
}
