// ship_layout.cpp — the layout table (see ship_layout.h).
#include "sim/ship_layout.h"

#include "sim/ship.h"

#include <algorithm>

const std::vector<ShipLayout>& all_layouts() {
    using enum ItemKind;
    // Each one: a bow tapering to the top, a wide body with the bridge in
    // its middle, and the engines along the stern, symmetric about the
    // middle.
    static const std::vector<ShipLayout> layouts = {
        {"skiff",
         "Skiff",
         "A small, simple hull: room for a first factory.",
         {{{14, 0}, {12, 4}}, {{8, 4}, {24, 4}}, {{0, 8}, {40, 24}}, {{4, 32}, {32, 4}}},
         {17, 15},
         {{10, 33}, {28, 33}},
         {{IronOre, 200}, {IronPlate, 100}, {CopperPlate, 50}},
         std::nullopt,
         2,
         1},
        {"hauler",
         "Hauler",
         "Long and roomy, for long production lines.",
         {{{16, 0}, {16, 6}}, {{8, 6}, {32, 8}}, {{0, 14}, {48, 58}}, {{6, 72}, {36, 8}}},
         {21, 24},
         {{11, 77}, {23, 77}, {35, 77}},
         {{IronOre, 300}, {Silica, 200}, {Steel, 100}, {StructuralFrame, 20}},
         milestone_index("Deep space"),
         2,
         2},
        {"frigate",
         "Frigate",
         "A broad arrowhead with space for industry and weapons.",
         {{{30, 0}, {12, 8}}, {{22, 8}, {28, 10}}, {{12, 18}, {48, 12}}, {{0, 30}, {72, 30}}, {{8, 60}, {56, 12}}},
         {33, 36},
         {{14, 69}, {26, 69}, {44, 69}, {56, 69}},
         {{IronOre, 400}, {Carbon, 200}, {Steel, 200}, {StructuralFrame, 40}, {AdvancedCircuit, 20}},
         milestone_index("Escape velocity"),
         3,
         2},
        {"carrier",
         "Carrier",
         "A capital ship: nearly the whole build grid.",
         {{{36, 0}, {40, 10}}, {{16, 10}, {80, 14}}, {{0, 24}, {112, 60}}, {{12, 84}, {88, 12}}},
         {53, 44},
         {{18, 93}, {34, 93}, {50, 93}, {60, 93}, {76, 93}, {92, 93}},
         {{IronOre, 1000}, {IridiumOre, 300}, {Steel, 300}, {IridiumPlate, 50}, {Processor, 20}},
         milestone_index("Frontier"),
         4,
         3},
    };
    return layouts;
}

const ShipLayout& starter_layout() {
    return all_layouts().front();
}

const std::vector<ShipLayout>& enemy_layouts() {
    // Like the player's, but small: room for a bridge, a few weapons and
    // shields, and not much else.
    static const std::vector<ShipLayout> layouts = {
        {"scout",
         "Scout",
         "An enemy picket ship.",
         {{{4, 0}, {6, 3}}, {{0, 3}, {14, 10}}, {{2, 13}, {10, 3}}},
         {4, 5},
         {{2, 13}, {10, 13}},
         {},
         std::nullopt,
         1,
         0},
        {"raider",
         "Raider",
         "An enemy raider.",
         {{{6, 0}, {8, 4}}, {{0, 4}, {20, 12}}, {{3, 16}, {14, 3}}},
         {7, 7},
         {{3, 16}, {9, 16}, {15, 16}},
         {},
         std::nullopt,
         2,
         1},
        {"gunship",
         "Gunship",
         "An enemy gunship.",
         {{{7, 0}, {10, 4}}, {{0, 4}, {24, 14}}, {{4, 18}, {16, 3}}},
         {9, 8},
         {{4, 18}, {11, 18}, {18, 18}},
         {},
         std::nullopt,
         2,
         1},
        {"corsair",
         "Corsair",
         "An enemy corsair.",
         {{{8, 0}, {12, 5}}, {{0, 5}, {28, 16}}, {{4, 21}, {20, 3}}},
         {11, 10},
         {{4, 21}, {10, 21}, {16, 21}, {22, 21}},
         {},
         std::nullopt,
         3,
         2},
        {"dreadnought",
         "Dreadnought",
         "An enemy capital ship.",
         {{{10, 0}, {14, 5}}, {{3, 5}, {28, 6}}, {{0, 11}, {34, 16}}, {{4, 27}, {26, 3}}},
         {14, 15},
         {{4, 27}, {10, 27}, {16, 27}, {22, 27}, {28, 27}},
         {},
         std::nullopt,
         3,
         3},
    };
    return layouts;
}

const ShipLayout* find_layout(std::string_view id) {
    for (const std::vector<ShipLayout>* list : {&all_layouts(), &enemy_layouts()})
        for (const ShipLayout& layout : *list)
            if (layout.id == id)
                return &layout;
    return nullptr;
}

GridSize layout_bounds(const ShipLayout& layout) {
    GridSize size{0, 0};
    for (const HullRect& r : layout.hull)
        size = {std::max(size.w, r.at.x + r.size.w), std::max(size.h, r.at.y + r.size.h)};
    return size;
}

bool layout_has_hull(const ShipLayout& layout, GridPos p) {
    return std::any_of(layout.hull.begin(), layout.hull.end(), [&](const HullRect& r) {
        return p.x >= r.at.x && p.y >= r.at.y && p.x < r.at.x + r.size.w && p.y < r.at.y + r.size.h;
    });
}

int layout_hull_count(const ShipLayout& layout) {
    const GridSize size = layout_bounds(layout);
    int n = 0;
    for (int y = 0; y < size.h; ++y)
        for (int x = 0; x < size.w; ++x)
            n += layout_has_hull(layout, {x, y}) ? 1 : 0;
    return n;
}

GridPos layout_origin(const ShipLayout& layout) {
    const GridSize size = layout_bounds(layout);
    return {(kShipGridSize - size.w) / 2, (kShipGridSize - size.h) / 2};
}

GridPos layout_to_grid(const ShipLayout& layout, GridPos p) {
    const GridPos origin = layout_origin(layout);
    return {origin.x + p.x, origin.y + p.y};
}

bool layout_unlocked(const ShipLayout& layout, const Progress& progress) {
    return !layout.milestone || progress.current_index() > *layout.milestone;
}

std::optional<Port> port_of(ItemKind kind) {
    if (is_weapon(kind))
        return Port::Weapon;
    if (kind == ItemKind::ShieldProjector)
        return Port::Shield;
    return std::nullopt;
}

int layout_ports(const ShipLayout& layout, Port port) {
    return port == Port::Weapon ? layout.weapon_ports : layout.shield_ports;
}

const char* port_name(Port port) {
    return port == Port::Weapon ? "weapon" : "shield";
}
