// test_ship_layout.cpp — headless tests for the ship layout table: every
// layout fits the build grid, with its bridge and engines on its hull and
// clear of each other; ids are unique; the starter is the smallest; and the
// bigger ones unlock with ever deeper deliveries.
#include "sim/building.h"
#include "sim/deliveries.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"

#include <cassert>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace {

// Whether `layout` has hull under every cell of the box at `at`, `size`.
bool on_hull(const ShipLayout& layout, GridPos at, GridSize size) {
    for (int y = at.y; y < at.y + size.h; ++y)
        for (int x = at.x; x < at.x + size.w; ++x)
            if (!layout_has_hull(layout, {x, y}))
                return false;
    return true;
}

void test_table() {
    const std::vector<ShipLayout>& layouts = all_layouts();
    assert(!layouts.empty());
    assert(&starter_layout() == &layouts.front());
    std::set<std::string> ids;
    int previous_hull = 0;
    for (const ShipLayout& layout : layouts) {
        assert(ids.insert(layout.id).second);
        assert(find_layout(layout.id) == &layout);

        // On the grid, from (0, 0).
        const GridSize bounds = layout_bounds(layout);
        assert(bounds.w > 0 && bounds.h > 0 && bounds.w <= kShipGridSize && bounds.h <= kShipGridSize);
        const GridPos origin = layout_origin(layout);
        assert(origin.x >= 0 && origin.y >= 0);
        for (const HullRect& r : layout.hull)
            assert(r.at.x >= 0 && r.at.y >= 0 && r.size.w > 0 && r.size.h > 0);

        // Its bridge and engines on its hull, clear of each other.
        std::vector<std::pair<GridPos, GridSize>> parts{{layout.bridge, {kBridgeSize, kBridgeSize}}};
        for (const GridPos e : layout.engines)
            parts.push_back({e, footprint_of(ItemKind::IonThruster)});
        assert(!layout.engines.empty());
        for (std::size_t i = 0; i < parts.size(); ++i) {
            const auto [at, size] = parts[i];
            assert(on_hull(layout, at, size));
            for (std::size_t j = 0; j < i; ++j) {
                const auto [other, other_size] = parts[j];
                assert(at.x + size.w <= other.x || other.x + other_size.w <= at.x || at.y + size.h <= other.y ||
                       other.y + other_size.h <= at.y);
            }
        }

        // Smallest first, each one costing something.
        const int hull = layout_hull_count(layout);
        assert(hull > previous_hull);
        previous_hull = hull;
        assert(!layout.cost.empty());
        for (const ItemStack& s : layout.cost)
            assert(s.count > 0 && !is_layout_part(s.kind));
    }
}

void test_unlocks() {
    // The starter from the start; the rest by their tech, each further
    // along the tech tree than the one before.
    Progress progress;
    assert(!starter_layout().tech && layout_unlocked(starter_layout(), progress));
    std::optional<std::size_t> previous;
    for (const ShipLayout& layout : all_layouts()) {
        if (!layout.tech)
            continue;
        assert(*layout.tech < all_techs().size() && (!previous || *layout.tech > *previous));
        previous = layout.tech;
        assert(!layout_unlocked(layout, progress));
    }
    for (std::size_t t = 0; t < all_techs().size(); ++t)
        assert(progress.grant(t));
    for (const ShipLayout& layout : all_layouts())
        assert(layout_unlocked(layout, progress));

    // Exactly when its tech is bought.
    const ShipLayout& hauler = *find_layout("hauler");
    Progress partway;
    for (std::size_t t = 0; !partway.for_sale(*hauler.tech); ++t)
        partway.grant(t);
    assert(!layout_unlocked(hauler, partway));
    partway.add_points(all_techs()[*hauler.tech].cost);
    assert(partway.buy(*hauler.tech));
    assert(layout_unlocked(hauler, partway));
    assert(!find_layout("battlestar"));
}

}  // namespace

int main() {
    test_table();
    test_unlocks();
    std::puts("test_ship_layout: all passed");
    return 0;
}
